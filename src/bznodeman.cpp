// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activebznode.h"
#include "addrman.h"
#include "darksend.h"
//#include "governance.h"
#include "bznode-payments.h"
#include "bznode-sync.h"
#include "bznodeman.h"
#include "netfulfilledman.h"
#include "util.h"

/** Bznode manager */
CBznodeMan mnodeman;

const std::string CBznodeMan::SERIALIZATION_VERSION_STRING = "CBznodeMan-Version-4";

struct CompareLastPaidBlock
{
    bool operator()(const std::pair<int, CBznode*>& t1,
                    const std::pair<int, CBznode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

struct CompareScoreMN
{
    bool operator()(const std::pair<int64_t, CBznode*>& t1,
                    const std::pair<int64_t, CBznode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

CBznodeIndex::CBznodeIndex()
    : nSize(0),
      mapIndex(),
      mapReverseIndex()
{}

bool CBznodeIndex::Get(int nIndex, CTxIn& vinBznode) const
{
    rindex_m_cit it = mapReverseIndex.find(nIndex);
    if(it == mapReverseIndex.end()) {
        return false;
    }
    vinBznode = it->second;
    return true;
}

int CBznodeIndex::GetBznodeIndex(const CTxIn& vinBznode) const
{
    index_m_cit it = mapIndex.find(vinBznode);
    if(it == mapIndex.end()) {
        return -1;
    }
    return it->second;
}

void CBznodeIndex::AddBznodeVIN(const CTxIn& vinBznode)
{
    index_m_it it = mapIndex.find(vinBznode);
    if(it != mapIndex.end()) {
        return;
    }
    int nNextIndex = nSize;
    mapIndex[vinBznode] = nNextIndex;
    mapReverseIndex[nNextIndex] = vinBznode;
    ++nSize;
}

void CBznodeIndex::Clear()
{
    mapIndex.clear();
    mapReverseIndex.clear();
    nSize = 0;
}
struct CompareByAddr

{
    bool operator()(const CBznode* t1,
                    const CBznode* t2) const
    {
        return t1->addr < t2->addr;
    }
};

void CBznodeIndex::RebuildIndex()
{
    nSize = mapIndex.size();
    for(index_m_it it = mapIndex.begin(); it != mapIndex.end(); ++it) {
        mapReverseIndex[it->second] = it->first;
    }
}

CBznodeMan::CBznodeMan() : cs(),
  vBznodes(),
  mAskedUsForBznodeList(),
  mWeAskedForBznodeList(),
  mWeAskedForBznodeListEntry(),
  mWeAskedForVerification(),
  mMnbRecoveryRequests(),
  mMnbRecoveryGoodReplies(),
  listScheduledMnbRequestConnections(),
  nLastIndexRebuildTime(0),
  indexBznodes(),
  indexBznodesOld(),
  fIndexRebuilt(false),
  fBznodesAdded(false),
  fBznodesRemoved(false),
//  vecDirtyGovernanceObjectHashes(),
  nLastWatchdogVoteTime(0),
  mapSeenBznodeBroadcast(),
  mapSeenBznodePing(),
  nDsqCount(0)
{}

bool CBznodeMan::Add(CBznode &mn)
{
    LOCK(cs);

    CBznode *pmn = Find(mn.vin);
    if (pmn == NULL) {
        LogPrint("bznode", "CBznodeMan::Add -- Adding new Bznode: addr=%s, %i now\n", mn.addr.ToString(), size() + 1);
        vBznodes.push_back(mn);
        indexBznodes.AddBznodeVIN(mn.vin);
        fBznodesAdded = true;
        return true;
    }

    return false;
}

void CBznodeMan::AskForMN(CNode* pnode, const CTxIn &vin)
{
    if(!pnode) return;

    LOCK(cs);

    std::map<COutPoint, std::map<CNetAddr, int64_t> >::iterator it1 = mWeAskedForBznodeListEntry.find(vin.prevout);
    if (it1 != mWeAskedForBznodeListEntry.end()) {
        std::map<CNetAddr, int64_t>::iterator it2 = it1->second.find(pnode->addr);
        if (it2 != it1->second.end()) {
            if (GetTime() < it2->second) {
                // we've asked recently, should not repeat too often or we could get banned
                return;
            }
            // we asked this node for this outpoint but it's ok to ask again already
            LogPrintf("CBznodeMan::AskForMN -- Asking same peer %s for missing bznode entry again: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
        } else {
            // we already asked for this outpoint but not this node
            LogPrintf("CBznodeMan::AskForMN -- Asking new peer %s for missing bznode entry: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
        }
    } else {
        // we never asked any node for this outpoint
        LogPrintf("CBznodeMan::AskForMN -- Asking peer %s for missing bznode entry for the first time: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
    }
    mWeAskedForBznodeListEntry[vin.prevout][pnode->addr] = GetTime() + DSEG_UPDATE_SECONDS;

    pnode->PushMessage(NetMsgType::DSEG, vin);
}

void CBznodeMan::Check()
{
    LOCK(cs);

//    LogPrint("bznode", "CBznodeMan::Check -- nLastWatchdogVoteTime=%d, IsWatchdogActive()=%d\n", nLastWatchdogVoteTime, IsWatchdogActive());

    BOOST_FOREACH(CBznode& mn, vBznodes) {
        mn.Check();
    }
}

void CBznodeMan::CheckAndRemove()
{
    if(!bznodeSync.IsBznodeListSynced()) return;

    LogPrintf("CBznodeMan::CheckAndRemove\n");

    {
        // Need LOCK2 here to ensure consistent locking order because code below locks cs_main
        // in CheckMnbAndUpdateBznodeList()
        LOCK2(cs_main, cs);

        Check();

        // Remove spent bznodes, prepare structures and make requests to reasure the state of inactive ones
        std::vector<CBznode>::iterator it = vBznodes.begin();
        std::vector<std::pair<int, CBznode> > vecBznodeRanks;
        // ask for up to MNB_RECOVERY_MAX_ASK_ENTRIES bznode entries at a time
        int nAskForMnbRecovery = MNB_RECOVERY_MAX_ASK_ENTRIES;
        while(it != vBznodes.end()) {
            CBznodeBroadcast mnb = CBznodeBroadcast(*it);
            uint256 hash = mnb.GetHash();
            // If collateral was spent ...
            if ((*it).IsOutpointSpent()) {
                LogPrint("bznode", "CBznodeMan::CheckAndRemove -- Removing Bznode: %s  addr=%s  %i now\n", (*it).GetStateString(), (*it).addr.ToString(), size() - 1);

                // erase all of the broadcasts we've seen from this txin, ...
                mapSeenBznodeBroadcast.erase(hash);
                mWeAskedForBznodeListEntry.erase((*it).vin.prevout);

                // and finally remove it from the list
//                it->FlagGovernanceItemsAsDirty();
                it = vBznodes.erase(it);
                fBznodesRemoved = true;
            } else {
                bool fAsk = pCurrentBlockIndex &&
                            (nAskForMnbRecovery > 0) &&
                            bznodeSync.IsSynced() &&
                            it->IsNewStartRequired() &&
                            !IsMnbRecoveryRequested(hash);
                if(fAsk) {
                    // this mn is in a non-recoverable state and we haven't asked other nodes yet
                    std::set<CNetAddr> setRequested;
                    // calulate only once and only when it's needed
                    if(vecBznodeRanks.empty()) {
                        int nRandomBlockHeight = GetRandInt(pCurrentBlockIndex->nHeight);
                        vecBznodeRanks = GetBznodeRanks(nRandomBlockHeight);
                    }
                    bool fAskedForMnbRecovery = false;
                    // ask first MNB_RECOVERY_QUORUM_TOTAL bznodes we can connect to and we haven't asked recently
                    for(int i = 0; setRequested.size() < MNB_RECOVERY_QUORUM_TOTAL && i < (int)vecBznodeRanks.size(); i++) {
                        // avoid banning
                        if(mWeAskedForBznodeListEntry.count(it->vin.prevout) && mWeAskedForBznodeListEntry[it->vin.prevout].count(vecBznodeRanks[i].second.addr)) continue;
                        // didn't ask recently, ok to ask now
                        CService addr = vecBznodeRanks[i].second.addr;
                        setRequested.insert(addr);
                        listScheduledMnbRequestConnections.push_back(std::make_pair(addr, hash));
                        fAskedForMnbRecovery = true;
                    }
                    if(fAskedForMnbRecovery) {
                        LogPrint("bznode", "CBznodeMan::CheckAndRemove -- Recovery initiated, bznode=%s\n", it->vin.prevout.ToStringShort());
                        nAskForMnbRecovery--;
                    }
                    // wait for mnb recovery replies for MNB_RECOVERY_WAIT_SECONDS seconds
                    mMnbRecoveryRequests[hash] = std::make_pair(GetTime() + MNB_RECOVERY_WAIT_SECONDS, setRequested);
                }
                ++it;
            }
        }

        // proces replies for BZNODE_NEW_START_REQUIRED bznodes
        LogPrint("bznode", "CBznodeMan::CheckAndRemove -- mMnbRecoveryGoodReplies size=%d\n", (int)mMnbRecoveryGoodReplies.size());
        std::map<uint256, std::vector<CBznodeBroadcast> >::iterator itMnbReplies = mMnbRecoveryGoodReplies.begin();
        while(itMnbReplies != mMnbRecoveryGoodReplies.end()){
            if(mMnbRecoveryRequests[itMnbReplies->first].first < GetTime()) {
                // all nodes we asked should have replied now
                if(itMnbReplies->second.size() >= MNB_RECOVERY_QUORUM_REQUIRED) {
                    // majority of nodes we asked agrees that this mn doesn't require new mnb, reprocess one of new mnbs
                    LogPrint("bznode", "CBznodeMan::CheckAndRemove -- reprocessing mnb, bznode=%s\n", itMnbReplies->second[0].vin.prevout.ToStringShort());
                    // mapSeenBznodeBroadcast.erase(itMnbReplies->first);
                    int nDos;
                    itMnbReplies->second[0].fRecovery = true;
                    CheckMnbAndUpdateBznodeList(NULL, itMnbReplies->second[0], nDos);
                }
                LogPrint("bznode", "CBznodeMan::CheckAndRemove -- removing mnb recovery reply, bznode=%s, size=%d\n", itMnbReplies->second[0].vin.prevout.ToStringShort(), (int)itMnbReplies->second.size());
                mMnbRecoveryGoodReplies.erase(itMnbReplies++);
            } else {
                ++itMnbReplies;
            }
        }
    }
    {
        // no need for cm_main below
        LOCK(cs);

        std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > >::iterator itMnbRequest = mMnbRecoveryRequests.begin();
        while(itMnbRequest != mMnbRecoveryRequests.end()){
            // Allow this mnb to be re-verified again after MNB_RECOVERY_RETRY_SECONDS seconds
            // if mn is still in BZNODE_NEW_START_REQUIRED state.
            if(GetTime() - itMnbRequest->second.first > MNB_RECOVERY_RETRY_SECONDS) {
                mMnbRecoveryRequests.erase(itMnbRequest++);
            } else {
                ++itMnbRequest;
            }
        }

        // check who's asked for the Bznode list
        std::map<CNetAddr, int64_t>::iterator it1 = mAskedUsForBznodeList.begin();
        while(it1 != mAskedUsForBznodeList.end()){
            if((*it1).second < GetTime()) {
                mAskedUsForBznodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check who we asked for the Bznode list
        it1 = mWeAskedForBznodeList.begin();
        while(it1 != mWeAskedForBznodeList.end()){
            if((*it1).second < GetTime()){
                mWeAskedForBznodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check which Bznodes we've asked for
        std::map<COutPoint, std::map<CNetAddr, int64_t> >::iterator it2 = mWeAskedForBznodeListEntry.begin();
        while(it2 != mWeAskedForBznodeListEntry.end()){
            std::map<CNetAddr, int64_t>::iterator it3 = it2->second.begin();
            while(it3 != it2->second.end()){
                if(it3->second < GetTime()){
                    it2->second.erase(it3++);
                } else {
                    ++it3;
                }
            }
            if(it2->second.empty()) {
                mWeAskedForBznodeListEntry.erase(it2++);
            } else {
                ++it2;
            }
        }

        std::map<CNetAddr, CBznodeVerification>::iterator it3 = mWeAskedForVerification.begin();
        while(it3 != mWeAskedForVerification.end()){
            if(it3->second.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS) {
                mWeAskedForVerification.erase(it3++);
            } else {
                ++it3;
            }
        }

        // NOTE: do not expire mapSeenBznodeBroadcast entries here, clean them on mnb updates!

        // remove expired mapSeenBznodePing
        std::map<uint256, CBznodePing>::iterator it4 = mapSeenBznodePing.begin();
        while(it4 != mapSeenBznodePing.end()){
            if((*it4).second.IsExpired()) {
                LogPrint("bznode", "CBznodeMan::CheckAndRemove -- Removing expired Bznode ping: hash=%s\n", (*it4).second.GetHash().ToString());
                mapSeenBznodePing.erase(it4++);
            } else {
                ++it4;
            }
        }

        // remove expired mapSeenBznodeVerification
        std::map<uint256, CBznodeVerification>::iterator itv2 = mapSeenBznodeVerification.begin();
        while(itv2 != mapSeenBznodeVerification.end()){
            if((*itv2).second.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS){
                LogPrint("bznode", "CBznodeMan::CheckAndRemove -- Removing expired Bznode verification: hash=%s\n", (*itv2).first.ToString());
                mapSeenBznodeVerification.erase(itv2++);
            } else {
                ++itv2;
            }
        }

        LogPrintf("CBznodeMan::CheckAndRemove -- %s\n", ToString());

        if(fBznodesRemoved) {
            CheckAndRebuildBznodeIndex();
        }
    }

    if(fBznodesRemoved) {
        NotifyBznodeUpdates();
    }
}

void CBznodeMan::Clear()
{
    LOCK(cs);
    vBznodes.clear();
    mAskedUsForBznodeList.clear();
    mWeAskedForBznodeList.clear();
    mWeAskedForBznodeListEntry.clear();
    mapSeenBznodeBroadcast.clear();
    mapSeenBznodePing.clear();
    nDsqCount = 0;
    nLastWatchdogVoteTime = 0;
    indexBznodes.Clear();
    indexBznodesOld.Clear();
}

int CBznodeMan::CountBznodes(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinBznodePaymentsProto() : nProtocolVersion;

    BOOST_FOREACH(CBznode& mn, vBznodes) {
        if(mn.nProtocolVersion < nProtocolVersion) continue;
        nCount++;
    }

    return nCount;
}

int CBznodeMan::CountEnabled(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinBznodePaymentsProto() : nProtocolVersion;

    BOOST_FOREACH(CBznode& mn, vBznodes) {
        if(mn.nProtocolVersion < nProtocolVersion || !mn.IsEnabled()) continue;
        nCount++;
    }

    return nCount;
}

/* Only IPv4 bznodes are allowed in 12.1, saving this for later
int CBznodeMan::CountByIP(int nNetworkType)
{
    LOCK(cs);
    int nNodeCount = 0;

    BOOST_FOREACH(CBznode& mn, vBznodes)
        if ((nNetworkType == NET_IPV4 && mn.addr.IsIPv4()) ||
            (nNetworkType == NET_TOR  && mn.addr.IsTor())  ||
            (nNetworkType == NET_IPV6 && mn.addr.IsIPv6())) {
                nNodeCount++;
        }

    return nNodeCount;
}
*/

void CBznodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForBznodeList.find(pnode->addr);
            if(it != mWeAskedForBznodeList.end() && GetTime() < (*it).second) {
                LogPrintf("CBznodeMan::DsegUpdate -- we already asked %s for the list; skipping...\n", pnode->addr.ToString());
                return;
            }
        }
    }
    
    pnode->PushMessage(NetMsgType::DSEG, CTxIn());
    int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
    mWeAskedForBznodeList[pnode->addr] = askAgain;

    LogPrint("bznode", "CBznodeMan::DsegUpdate -- asked %s for the list\n", pnode->addr.ToString());
}

CBznode* CBznodeMan::Find(const CScript &payee)
{
    LOCK(cs);

    BOOST_FOREACH(CBznode& mn, vBznodes)
    {
        if(GetScriptForDestination(mn.pubKeyCollateralAddress.GetID()) == payee)
            return &mn;
    }
    return NULL;
}

CBznode* CBznodeMan::Find(const CTxIn &vin)
{
    LOCK(cs);

    BOOST_FOREACH(CBznode& mn, vBznodes)
    {
        if(mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}

CBznode* CBznodeMan::Find(const CPubKey &pubKeyBznode)
{
    LOCK(cs);

    BOOST_FOREACH(CBznode& mn, vBznodes)
    {
        if(mn.pubKeyBznode == pubKeyBznode)
            return &mn;
    }
    return NULL;
}

bool CBznodeMan::Get(const CPubKey& pubKeyBznode, CBznode& bznode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    CBznode* pMN = Find(pubKeyBznode);
    if(!pMN)  {
        return false;
    }
    bznode = *pMN;
    return true;
}

bool CBznodeMan::Get(const CTxIn& vin, CBznode& bznode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    CBznode* pMN = Find(vin);
    if(!pMN)  {
        return false;
    }
    bznode = *pMN;
    return true;
}

bznode_info_t CBznodeMan::GetBznodeInfo(const CTxIn& vin)
{
    bznode_info_t info;
    LOCK(cs);
    CBznode* pMN = Find(vin);
    if(!pMN)  {
        return info;
    }
    info = pMN->GetInfo();
    return info;
}

bznode_info_t CBznodeMan::GetBznodeInfo(const CPubKey& pubKeyBznode)
{
    bznode_info_t info;
    LOCK(cs);
    CBznode* pMN = Find(pubKeyBznode);
    if(!pMN)  {
        return info;
    }
    info = pMN->GetInfo();
    return info;
}

bool CBznodeMan::Has(const CTxIn& vin)
{
    LOCK(cs);
    CBznode* pMN = Find(vin);
    return (pMN != NULL);
}

char* CBznodeMan::GetNotQualifyReason(CBznode& mn, int nBlockHeight, bool fFilterSigTime, int nMnCount)
{
    if (!mn.IsValidForPayment()) {
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'not valid for payment'");
        return reasonStr;
    }
    // //check protocol version
    if (mn.nProtocolVersion < mnpayments.GetMinBznodePaymentsProto()) {
        // LogPrintf("Invalid nProtocolVersion!\n");
        // LogPrintf("mn.nProtocolVersion=%s!\n", mn.nProtocolVersion);
        // LogPrintf("mnpayments.GetMinBznodePaymentsProto=%s!\n", mnpayments.GetMinBznodePaymentsProto());
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'Invalid nProtocolVersion', nProtocolVersion=%d", mn.nProtocolVersion);
        return reasonStr;
    }
    //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
    if (mnpayments.IsScheduled(mn, nBlockHeight)) {
        // LogPrintf("mnpayments.IsScheduled!\n");
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'is scheduled'");
        return reasonStr;
    }
    //it's too new, wait for a cycle
    if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) {
        // LogPrintf("it's too new, wait for a cycle!\n");
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'too new', sigTime=%s, will be qualifed after=%s",
                DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime).c_str(), DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime + (nMnCount * 2.6 * 60)).c_str());
        return reasonStr;
    }
    //make sure it has at least as many confirmations as there are bznodes
    if (mn.GetCollateralAge() < nMnCount) {
        // LogPrintf("mn.GetCollateralAge()=%s!\n", mn.GetCollateralAge());
        // LogPrintf("nMnCount=%s!\n", nMnCount);
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'collateralAge < xnCount', collateralAge=%d, xnCount=%d", mn.GetCollateralAge(), nMnCount);
        return reasonStr;
    }
    return NULL;
}

//
// Deterministically select the oldest/best bznode to pay on the network
//
CBznode* CBznodeMan::GetNextBznodeInQueueForPayment(bool fFilterSigTime, int& nCount)
{
    if(!pCurrentBlockIndex) {
        nCount = 0;
        return NULL;
    }
    return GetNextBznodeInQueueForPayment(pCurrentBlockIndex->nHeight, fFilterSigTime, nCount);
}

CBznode* CBznodeMan::GetNextBznodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    // Need LOCK2 here to ensure consistent locking order because the GetBlockHash call below locks cs_main
    LOCK2(cs_main,cs);

    CBznode *pBestBznode = NULL;
    std::vector<std::pair<int, CBznode*> > vecBznodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */
    int nMnCount = CountEnabled();
    int index = 0;
    BOOST_FOREACH(CBznode &mn, vBznodes)
    {
        index += 1;
        // LogPrintf("index=%s, mn=%s\n", index, mn.ToString());
        /*if (!mn.IsValidForPayment()) {
            LogPrint("bznodeman", "Bznode, %s, addr(%s), not-qualified: 'not valid for payment'\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            continue;
        }
        // //check protocol version
        if (mn.nProtocolVersion < mnpayments.GetMinBznodePaymentsProto()) {
            // LogPrintf("Invalid nProtocolVersion!\n");
            // LogPrintf("mn.nProtocolVersion=%s!\n", mn.nProtocolVersion);
            // LogPrintf("mnpayments.GetMinBznodePaymentsProto=%s!\n", mnpayments.GetMinBznodePaymentsProto());
            LogPrint("bznodeman", "Bznode, %s, addr(%s), not-qualified: 'invalid nProtocolVersion'\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            continue;
        }
        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (mnpayments.IsScheduled(mn, nBlockHeight)) {
            // LogPrintf("mnpayments.IsScheduled!\n");
            LogPrint("bznodeman", "Bznode, %s, addr(%s), not-qualified: 'IsScheduled'\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            continue;
        }
        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) {
            // LogPrintf("it's too new, wait for a cycle!\n");
            LogPrint("bznodeman", "Bznode, %s, addr(%s), not-qualified: 'it's too new, wait for a cycle!', sigTime=%s, will be qualifed after=%s\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString(), DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime).c_str(), DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime + (nMnCount * 2.6 * 60)).c_str());
            continue;
        }
        //make sure it has at least as many confirmations as there are bznodes
        if (mn.GetCollateralAge() < nMnCount) {
            // LogPrintf("mn.GetCollateralAge()=%s!\n", mn.GetCollateralAge());
            // LogPrintf("nMnCount=%s!\n", nMnCount);
            LogPrint("bznodeman", "Bznode, %s, addr(%s), not-qualified: 'mn.GetCollateralAge() < nMnCount', CollateralAge=%d, nMnCount=%d\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString(), mn.GetCollateralAge(), nMnCount);
            continue;
        }*/
        char* reasonStr = GetNotQualifyReason(mn, nBlockHeight, fFilterSigTime, nMnCount);
        if (reasonStr != NULL) {
            LogPrint("bznodeman", "Bznode, %s, addr(%s), qualify %s\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString(), reasonStr);
            delete [] reasonStr;
            continue;
        }
        vecBznodeLastPaid.push_back(std::make_pair(mn.GetLastPaidBlock(), &mn));
    }
    nCount = (int)vecBznodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if(fFilterSigTime && nCount < nMnCount / 3) {
        // LogPrintf("Need Return, nCount=%s, nMnCount/3=%s\n", nCount, nMnCount/3);
        return GetNextBznodeInQueueForPayment(nBlockHeight, false, nCount);
    }

    // Sort them low to high
    sort(vecBznodeLastPaid.begin(), vecBznodeLastPaid.end(), CompareLastPaidBlock());

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight - 101)) {
        LogPrintf("CBznode::GetNextBznodeInQueueForPayment -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight - 101);
        return NULL;
    }
    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = nMnCount/10;
    int nCountTenth = 0;
    arith_uint256 nHighest = 0;
    BOOST_FOREACH (PAIRTYPE(int, CBznode*)& s, vecBznodeLastPaid){
        arith_uint256 nScore = s.second->CalculateScore(blockHash);
        if(nScore > nHighest){
            nHighest = nScore;
            pBestBznode = s.second;
        }
        nCountTenth++;
        if(nCountTenth >= nTenthNetwork) break;
    }
    return pBestBznode;
}

CBznode* CBznodeMan::FindRandomNotInVec(const std::vector<CTxIn> &vecToExclude, int nProtocolVersion)
{
    LOCK(cs);

    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinBznodePaymentsProto() : nProtocolVersion;

    int nCountEnabled = CountEnabled(nProtocolVersion);
    int nCountNotExcluded = nCountEnabled - vecToExclude.size();

    LogPrintf("CBznodeMan::FindRandomNotInVec -- %d enabled bznodes, %d bznodes to choose from\n", nCountEnabled, nCountNotExcluded);
    if(nCountNotExcluded < 1) return NULL;

    // fill a vector of pointers
    std::vector<CBznode*> vpBznodesShuffled;
    BOOST_FOREACH(CBznode &mn, vBznodes) {
        vpBznodesShuffled.push_back(&mn);
    }

    InsecureRand insecureRand;
    // shuffle pointers
    std::random_shuffle(vpBznodesShuffled.begin(), vpBznodesShuffled.end(), insecureRand);
    bool fExclude;

    // loop through
    BOOST_FOREACH(CBznode* pmn, vpBznodesShuffled) {
        if(pmn->nProtocolVersion < nProtocolVersion || !pmn->IsEnabled()) continue;
        fExclude = false;
        BOOST_FOREACH(const CTxIn &txinToExclude, vecToExclude) {
            if(pmn->vin.prevout == txinToExclude.prevout) {
                fExclude = true;
                break;
            }
        }
        if(fExclude) continue;
        // found the one not in vecToExclude
        LogPrint("bznode", "CBznodeMan::FindRandomNotInVec -- found, bznode=%s\n", pmn->vin.prevout.ToStringShort());
        return pmn;
    }

    LogPrint("bznode", "CBznodeMan::FindRandomNotInVec -- failed\n");
    return NULL;
}

int CBznodeMan::GetBznodeRank(const CTxIn& vin, int nBlockHeight, int nMinProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CBznode*> > vecBznodeScores;

    //make sure we know about this block
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, nBlockHeight)) return -1;

    LOCK(cs);

    // scan for winner
    BOOST_FOREACH(CBznode& mn, vBznodes) {
        if(mn.nProtocolVersion < nMinProtocol) continue;
        if(fOnlyActive) {
            if(!mn.IsEnabled()) continue;
        }
        else {
            if(!mn.IsValidForPayment()) continue;
        }
        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecBznodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecBznodeScores.rbegin(), vecBznodeScores.rend(), CompareScoreMN());

    int nRank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CBznode*)& scorePair, vecBznodeScores) {
        nRank++;
        if(scorePair.second->vin.prevout == vin.prevout) return nRank;
    }

    return -1;
}

std::vector<std::pair<int, CBznode> > CBznodeMan::GetBznodeRanks(int nBlockHeight, int nMinProtocol)
{
    std::vector<std::pair<int64_t, CBznode*> > vecBznodeScores;
    std::vector<std::pair<int, CBznode> > vecBznodeRanks;

    //make sure we know about this block
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, nBlockHeight)) return vecBznodeRanks;

    LOCK(cs);

    // scan for winner
    BOOST_FOREACH(CBznode& mn, vBznodes) {

        if(mn.nProtocolVersion < nMinProtocol || !mn.IsEnabled()) continue;

        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecBznodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecBznodeScores.rbegin(), vecBznodeScores.rend(), CompareScoreMN());

    int nRank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CBznode*)& s, vecBznodeScores) {
        nRank++;
        vecBznodeRanks.push_back(std::make_pair(nRank, *s.second));
    }

    return vecBznodeRanks;
}

CBznode* CBznodeMan::GetBznodeByRank(int nRank, int nBlockHeight, int nMinProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CBznode*> > vecBznodeScores;

    LOCK(cs);

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight)) {
        LogPrintf("CBznode::GetBznodeByRank -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight);
        return NULL;
    }

    // Fill scores
    BOOST_FOREACH(CBznode& mn, vBznodes) {

        if(mn.nProtocolVersion < nMinProtocol) continue;
        if(fOnlyActive && !mn.IsEnabled()) continue;

        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecBznodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecBznodeScores.rbegin(), vecBznodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CBznode*)& s, vecBznodeScores){
        rank++;
        if(rank == nRank) {
            return s.second;
        }
    }

    return NULL;
}

void CBznodeMan::ProcessBznodeConnections()
{
    //we don't care about this for regtest
    if(Params().NetworkIDString() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes) {
        if(pnode->fBznode) {
            if(darkSendPool.pSubmittedToBznode != NULL && pnode->addr == darkSendPool.pSubmittedToBznode->addr) continue;
            // LogPrintf("Closing Bznode connection: peer=%d, addr=%s\n", pnode->id, pnode->addr.ToString());
            pnode->fDisconnect = true;
        }
    }
}

std::pair<CService, std::set<uint256> > CBznodeMan::PopScheduledMnbRequestConnection()
{
    LOCK(cs);
    if(listScheduledMnbRequestConnections.empty()) {
        return std::make_pair(CService(), std::set<uint256>());
    }

    std::set<uint256> setResult;

    listScheduledMnbRequestConnections.sort();
    std::pair<CService, uint256> pairFront = listScheduledMnbRequestConnections.front();

    // squash hashes from requests with the same CService as the first one into setResult
    std::list< std::pair<CService, uint256> >::iterator it = listScheduledMnbRequestConnections.begin();
    while(it != listScheduledMnbRequestConnections.end()) {
        if(pairFront.first == it->first) {
            setResult.insert(it->second);
            it = listScheduledMnbRequestConnections.erase(it);
        } else {
            // since list is sorted now, we can be sure that there is no more hashes left
            // to ask for from this addr
            break;
        }
    }
    return std::make_pair(pairFront.first, setResult);
}


void CBznodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{

//    LogPrint("bznode", "CBznodeMan::ProcessMessage, strCommand=%s\n", strCommand);
    if(fLiteMode) return; // disable all BitcoinZero specific functionality
    if(!bznodeSync.IsBlockchainSynced()) return;

    if (strCommand == NetMsgType::MNANNOUNCE) { //Bznode Broadcast
        CBznodeBroadcast mnb;
        vRecv >> mnb;

        pfrom->setAskFor.erase(mnb.GetHash());

        LogPrintf("MNANNOUNCE -- Bznode announce, bznode=%s\n", mnb.vin.prevout.ToStringShort());

        int nDos = 0;

        if (CheckMnbAndUpdateBznodeList(pfrom, mnb, nDos)) {
            // use announced Bznode as a peer
            addrman.Add(CAddress(mnb.addr, NODE_NETWORK), pfrom->addr, 2*60*60);
        } else if(nDos > 0) {
            Misbehaving(pfrom->GetId(), nDos);
        }

        if(fBznodesAdded) {
            NotifyBznodeUpdates();
        }
    } else if (strCommand == NetMsgType::MNPING) { //Bznode Ping

        CBznodePing mnp;
        vRecv >> mnp;

        uint256 nHash = mnp.GetHash();

        pfrom->setAskFor.erase(nHash);

        LogPrint("bznode", "MNPING -- Bznode ping, bznode=%s\n", mnp.vin.prevout.ToStringShort());

        // Need LOCK2 here to ensure consistent locking order because the CheckAndUpdate call below locks cs_main
        LOCK2(cs_main, cs);

        if(mapSeenBznodePing.count(nHash)) return; //seen
        mapSeenBznodePing.insert(std::make_pair(nHash, mnp));

        LogPrint("bznode", "MNPING -- Bznode ping, bznode=%s new\n", mnp.vin.prevout.ToStringShort());

        // see if we have this Bznode
        CBznode* pmn = mnodeman.Find(mnp.vin);

        // too late, new MNANNOUNCE is required
        if(pmn && pmn->IsNewStartRequired()) return;

        int nDos = 0;
        if(mnp.CheckAndUpdate(pmn, false, nDos)) return;

        if(nDos > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDos);
        } else if(pmn != NULL) {
            // nothing significant failed, mn is a known one too
            return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a bznode entry once
        AskForMN(pfrom, mnp.vin);

    } else if (strCommand == NetMsgType::DSEG) { //Get Bznode list or specific entry
        // Ignore such requests until we are fully synced.
        // We could start processing this after bznode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!bznodeSync.IsSynced()) return;

        CTxIn vin;
        vRecv >> vin;

        LogPrint("bznode", "DSEG -- Bznode list, bznode=%s\n", vin.prevout.ToStringShort());

        LOCK(cs);

        if(vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if(!isLocal && Params().NetworkIDString() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForBznodeList.find(pfrom->addr);
                if (i != mAskedUsForBznodeList.end()){
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrintf("DSEG -- peer already asked me for the list, peer=%d\n", pfrom->id);
                        return;
                    }
                }
                int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
                mAskedUsForBznodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok

        int nInvCount = 0;

        BOOST_FOREACH(CBznode& mn, vBznodes) {
            if (vin != CTxIn() && vin != mn.vin) continue; // asked for specific vin but we are not there yet
            if (mn.addr.IsRFC1918() || mn.addr.IsLocal()) continue; // do not send local network bznode
            if (mn.IsUpdateRequired()) continue; // do not send outdated bznodes

            LogPrint("bznode", "DSEG -- Sending Bznode entry: bznode=%s  addr=%s\n", mn.vin.prevout.ToStringShort(), mn.addr.ToString());
            CBznodeBroadcast mnb = CBznodeBroadcast(mn);
            uint256 hash = mnb.GetHash();
            pfrom->PushInventory(CInv(MSG_BZNODE_ANNOUNCE, hash));
            pfrom->PushInventory(CInv(MSG_BZNODE_PING, mn.lastPing.GetHash()));
            nInvCount++;

            if (!mapSeenBznodeBroadcast.count(hash)) {
                mapSeenBznodeBroadcast.insert(std::make_pair(hash, std::make_pair(GetTime(), mnb)));
            }

            if (vin == mn.vin) {
                LogPrintf("DSEG -- Sent 1 Bznode inv to peer %d\n", pfrom->id);
                return;
            }
        }

        if(vin == CTxIn()) {
            pfrom->PushMessage(NetMsgType::SYNCSTATUSCOUNT, BZNODE_SYNC_LIST, nInvCount);
            LogPrintf("DSEG -- Sent %d Bznode invs to peer %d\n", nInvCount, pfrom->id);
            return;
        }
        // smth weird happen - someone asked us for vin we have no idea about?
        LogPrint("bznode", "DSEG -- No invs sent to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::MNVERIFY) { // Bznode Verify

        // Need LOCK2 here to ensure consistent locking order because the all functions below call GetBlockHash which locks cs_main
        LOCK2(cs_main, cs);

        CBznodeVerification mnv;
        vRecv >> mnv;

        if(mnv.vchSig1.empty()) {
            // CASE 1: someone asked me to verify myself /IP we are using/
            SendVerifyReply(pfrom, mnv);
        } else if (mnv.vchSig2.empty()) {
            // CASE 2: we _probably_ got verification we requested from some bznode
            ProcessVerifyReply(pfrom, mnv);
        } else {
            // CASE 3: we _probably_ got verification broadcast signed by some bznode which verified another one
            ProcessVerifyBroadcast(pfrom, mnv);
        }
    }
}

// Verification of bznodes via unique direct requests.

void CBznodeMan::DoFullVerificationStep()
{
    if(activeBznode.vin == CTxIn()) return;
    if(!bznodeSync.IsSynced()) return;

    std::vector<std::pair<int, CBznode> > vecBznodeRanks = GetBznodeRanks(pCurrentBlockIndex->nHeight - 1, MIN_POSE_PROTO_VERSION);

    // Need LOCK2 here to ensure consistent locking order because the SendVerifyRequest call below locks cs_main
    // through GetHeight() signal in ConnectNode
    LOCK2(cs_main, cs);

    int nCount = 0;

    int nMyRank = -1;
    int nRanksTotal = (int)vecBznodeRanks.size();

    // send verify requests only if we are in top MAX_POSE_RANK
    std::vector<std::pair<int, CBznode> >::iterator it = vecBznodeRanks.begin();
    while(it != vecBznodeRanks.end()) {
        if(it->first > MAX_POSE_RANK) {
            LogPrint("bznode", "CBznodeMan::DoFullVerificationStep -- Must be in top %d to send verify request\n",
                        (int)MAX_POSE_RANK);
            return;
        }
        if(it->second.vin == activeBznode.vin) {
            nMyRank = it->first;
            LogPrint("bznode", "CBznodeMan::DoFullVerificationStep -- Found self at rank %d/%d, verifying up to %d bznodes\n",
                        nMyRank, nRanksTotal, (int)MAX_POSE_CONNECTIONS);
            break;
        }
        ++it;
    }

    // edge case: list is too short and this bznode is not enabled
    if(nMyRank == -1) return;

    // send verify requests to up to MAX_POSE_CONNECTIONS bznodes
    // starting from MAX_POSE_RANK + nMyRank and using MAX_POSE_CONNECTIONS as a step
    int nOffset = MAX_POSE_RANK + nMyRank - 1;
    if(nOffset >= (int)vecBznodeRanks.size()) return;

    std::vector<CBznode*> vSortedByAddr;
    BOOST_FOREACH(CBznode& mn, vBznodes) {
        vSortedByAddr.push_back(&mn);
    }

    sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

    it = vecBznodeRanks.begin() + nOffset;
    while(it != vecBznodeRanks.end()) {
        if(it->second.IsPoSeVerified() || it->second.IsPoSeBanned()) {
            LogPrint("bznode", "CBznodeMan::DoFullVerificationStep -- Already %s%s%s bznode %s address %s, skipping...\n",
                        it->second.IsPoSeVerified() ? "verified" : "",
                        it->second.IsPoSeVerified() && it->second.IsPoSeBanned() ? " and " : "",
                        it->second.IsPoSeBanned() ? "banned" : "",
                        it->second.vin.prevout.ToStringShort(), it->second.addr.ToString());
            nOffset += MAX_POSE_CONNECTIONS;
            if(nOffset >= (int)vecBznodeRanks.size()) break;
            it += MAX_POSE_CONNECTIONS;
            continue;
        }
        LogPrint("bznode", "CBznodeMan::DoFullVerificationStep -- Verifying bznode %s rank %d/%d address %s\n",
                    it->second.vin.prevout.ToStringShort(), it->first, nRanksTotal, it->second.addr.ToString());
        if(SendVerifyRequest(CAddress(it->second.addr, NODE_NETWORK), vSortedByAddr)) {
            nCount++;
            if(nCount >= MAX_POSE_CONNECTIONS) break;
        }
        nOffset += MAX_POSE_CONNECTIONS;
        if(nOffset >= (int)vecBznodeRanks.size()) break;
        it += MAX_POSE_CONNECTIONS;
    }

    LogPrint("bznode", "CBznodeMan::DoFullVerificationStep -- Sent verification requests to %d bznodes\n", nCount);
}

// This function tries to find bznodes with the same addr,
// find a verified one and ban all the other. If there are many nodes
// with the same addr but none of them is verified yet, then none of them are banned.
// It could take many times to run this before most of the duplicate nodes are banned.

void CBznodeMan::CheckSameAddr()
{
    if(!bznodeSync.IsSynced() || vBznodes.empty()) return;

    std::vector<CBznode*> vBan;
    std::vector<CBznode*> vSortedByAddr;

    {
        LOCK(cs);

        CBznode* pprevBznode = NULL;
        CBznode* pverifiedBznode = NULL;

        BOOST_FOREACH(CBznode& mn, vBznodes) {
            vSortedByAddr.push_back(&mn);
        }

        sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

        BOOST_FOREACH(CBznode* pmn, vSortedByAddr) {
            // check only (pre)enabled bznodes
            if(!pmn->IsEnabled() && !pmn->IsPreEnabled()) continue;
            // initial step
            if(!pprevBznode) {
                pprevBznode = pmn;
                pverifiedBznode = pmn->IsPoSeVerified() ? pmn : NULL;
                continue;
            }
            // second+ step
            if(pmn->addr == pprevBznode->addr) {
                if(pverifiedBznode) {
                    // another bznode with the same ip is verified, ban this one
                    vBan.push_back(pmn);
                } else if(pmn->IsPoSeVerified()) {
                    // this bznode with the same ip is verified, ban previous one
                    vBan.push_back(pprevBznode);
                    // and keep a reference to be able to ban following bznodes with the same ip
                    pverifiedBznode = pmn;
                }
            } else {
                pverifiedBznode = pmn->IsPoSeVerified() ? pmn : NULL;
            }
            pprevBznode = pmn;
        }
    }

    // ban duplicates
    BOOST_FOREACH(CBznode* pmn, vBan) {
        LogPrintf("CBznodeMan::CheckSameAddr -- increasing PoSe ban score for bznode %s\n", pmn->vin.prevout.ToStringShort());
        pmn->IncreasePoSeBanScore();
    }
}

bool CBznodeMan::SendVerifyRequest(const CAddress& addr, const std::vector<CBznode*>& vSortedByAddr)
{
    if(netfulfilledman.HasFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request")) {
        // we already asked for verification, not a good idea to do this too often, skip it
        LogPrint("bznode", "CBznodeMan::SendVerifyRequest -- too many requests, skipping... addr=%s\n", addr.ToString());
        return false;
    }

    CNode* pnode = ConnectNode(addr, NULL, false, true);
    if(pnode == NULL) {
        LogPrintf("CBznodeMan::SendVerifyRequest -- can't connect to node to verify it, addr=%s\n", addr.ToString());
        return false;
    }

    netfulfilledman.AddFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request");
    // use random nonce, store it and require node to reply with correct one later
    CBznodeVerification mnv(addr, GetRandInt(999999), pCurrentBlockIndex->nHeight - 1);
    mWeAskedForVerification[addr] = mnv;
    LogPrintf("CBznodeMan::SendVerifyRequest -- verifying node using nonce %d addr=%s\n", mnv.nonce, addr.ToString());
    pnode->PushMessage(NetMsgType::MNVERIFY, mnv);

    return true;
}

void CBznodeMan::SendVerifyReply(CNode* pnode, CBznodeVerification& mnv)
{
    // only bznodes can sign this, why would someone ask regular node?
    if(!fBZNode) {
        // do not ban, malicious node might be using my IP
        // and trying to confuse the node which tries to verify it
        return;
    }

    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply")) {
//        // peer should not ask us that often
        LogPrintf("BznodeMan::SendVerifyReply -- ERROR: peer already asked me recently, peer=%d\n", pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        LogPrintf("BznodeMan::SendVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    std::string strMessage = strprintf("%s%d%s", activeBznode.service.ToString(), mnv.nonce, blockHash.ToString());

    if(!darkSendSigner.SignMessage(strMessage, mnv.vchSig1, activeBznode.keyBznode)) {
        LogPrintf("BznodeMan::SendVerifyReply -- SignMessage() failed\n");
        return;
    }

    std::string strError;

    if(!darkSendSigner.VerifyMessage(activeBznode.pubKeyBznode, mnv.vchSig1, strMessage, strError)) {
        LogPrintf("BznodeMan::SendVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
        return;
    }

    pnode->PushMessage(NetMsgType::MNVERIFY, mnv);
    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply");
}

void CBznodeMan::ProcessVerifyReply(CNode* pnode, CBznodeVerification& mnv)
{
    std::string strError;

    // did we even ask for it? if that's the case we should have matching fulfilled request
    if(!netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request")) {
        LogPrintf("CBznodeMan::ProcessVerifyReply -- ERROR: we didn't ask for verification of %s, peer=%d\n", pnode->addr.ToString(), pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nonce for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nonce != mnv.nonce) {
        LogPrintf("CBznodeMan::ProcessVerifyReply -- ERROR: wrong nounce: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nonce, mnv.nonce, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nBlockHeight for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nBlockHeight != mnv.nBlockHeight) {
        LogPrintf("CBznodeMan::ProcessVerifyReply -- ERROR: wrong nBlockHeight: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nBlockHeight, mnv.nBlockHeight, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("BznodeMan::ProcessVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

//    // we already verified this address, why node is spamming?
    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done")) {
        LogPrintf("CBznodeMan::ProcessVerifyReply -- ERROR: already verified %s recently\n", pnode->addr.ToString());
        Misbehaving(pnode->id, 20);
        return;
    }

    {
        LOCK(cs);

        CBznode* prealBznode = NULL;
        std::vector<CBznode*> vpBznodesToBan;
        std::vector<CBznode>::iterator it = vBznodes.begin();
        std::string strMessage1 = strprintf("%s%d%s", pnode->addr.ToString(), mnv.nonce, blockHash.ToString());
        while(it != vBznodes.end()) {
            if(CAddress(it->addr, NODE_NETWORK) == pnode->addr) {
                if(darkSendSigner.VerifyMessage(it->pubKeyBznode, mnv.vchSig1, strMessage1, strError)) {
                    // found it!
                    prealBznode = &(*it);
                    if(!it->IsPoSeVerified()) {
                        it->DecreasePoSeBanScore();
                    }
                    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done");

                    // we can only broadcast it if we are an activated bznode
                    if(activeBznode.vin == CTxIn()) continue;
                    // update ...
                    mnv.addr = it->addr;
                    mnv.vin1 = it->vin;
                    mnv.vin2 = activeBznode.vin;
                    std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(), mnv.nonce, blockHash.ToString(),
                                            mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());
                    // ... and sign it
                    if(!darkSendSigner.SignMessage(strMessage2, mnv.vchSig2, activeBznode.keyBznode)) {
                        LogPrintf("BznodeMan::ProcessVerifyReply -- SignMessage() failed\n");
                        return;
                    }

                    std::string strError;

                    if(!darkSendSigner.VerifyMessage(activeBznode.pubKeyBznode, mnv.vchSig2, strMessage2, strError)) {
                        LogPrintf("BznodeMan::ProcessVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
                        return;
                    }

                    mWeAskedForVerification[pnode->addr] = mnv;
                    mnv.Relay();

                } else {
                    vpBznodesToBan.push_back(&(*it));
                }
            }
            ++it;
        }
        // no real bznode found?...
        if(!prealBznode) {
            // this should never be the case normally,
            // only if someone is trying to game the system in some way or smth like that
            LogPrintf("CBznodeMan::ProcessVerifyReply -- ERROR: no real bznode found for addr %s\n", pnode->addr.ToString());
            Misbehaving(pnode->id, 20);
            return;
        }
        LogPrintf("CBznodeMan::ProcessVerifyReply -- verified real bznode %s for addr %s\n",
                    prealBznode->vin.prevout.ToStringShort(), pnode->addr.ToString());
        // increase ban score for everyone else
        BOOST_FOREACH(CBznode* pmn, vpBznodesToBan) {
            pmn->IncreasePoSeBanScore();
            LogPrint("bznode", "CBznodeMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                        prealBznode->vin.prevout.ToStringShort(), pnode->addr.ToString(), pmn->nPoSeBanScore);
        }
        LogPrintf("CBznodeMan::ProcessVerifyBroadcast -- PoSe score increased for %d fake bznodes, addr %s\n",
                    (int)vpBznodesToBan.size(), pnode->addr.ToString());
    }
}

void CBznodeMan::ProcessVerifyBroadcast(CNode* pnode, const CBznodeVerification& mnv)
{
    std::string strError;

    if(mapSeenBznodeVerification.find(mnv.GetHash()) != mapSeenBznodeVerification.end()) {
        // we already have one
        return;
    }
    mapSeenBznodeVerification[mnv.GetHash()] = mnv;

    // we don't care about history
    if(mnv.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS) {
        LogPrint("bznode", "BznodeMan::ProcessVerifyBroadcast -- Outdated: current block %d, verification block %d, peer=%d\n",
                    pCurrentBlockIndex->nHeight, mnv.nBlockHeight, pnode->id);
        return;
    }

    if(mnv.vin1.prevout == mnv.vin2.prevout) {
        LogPrint("bznode", "BznodeMan::ProcessVerifyBroadcast -- ERROR: same vins %s, peer=%d\n",
                    mnv.vin1.prevout.ToStringShort(), pnode->id);
        // that was NOT a good idea to cheat and verify itself,
        // ban the node we received such message from
        Misbehaving(pnode->id, 100);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("BznodeMan::ProcessVerifyBroadcast -- Can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    int nRank = GetBznodeRank(mnv.vin2, mnv.nBlockHeight, MIN_POSE_PROTO_VERSION);

    if (nRank == -1) {
        LogPrint("bznode", "CBznodeMan::ProcessVerifyBroadcast -- Can't calculate rank for bznode %s\n",
                    mnv.vin2.prevout.ToStringShort());
        return;
    }

    if(nRank > MAX_POSE_RANK) {
        LogPrint("bznode", "CBznodeMan::ProcessVerifyBroadcast -- Mastrernode %s is not in top %d, current rank %d, peer=%d\n",
                    mnv.vin2.prevout.ToStringShort(), (int)MAX_POSE_RANK, nRank, pnode->id);
        return;
    }

    {
        LOCK(cs);

        std::string strMessage1 = strprintf("%s%d%s", mnv.addr.ToString(), mnv.nonce, blockHash.ToString());
        std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(), mnv.nonce, blockHash.ToString(),
                                mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());

        CBznode* pmn1 = Find(mnv.vin1);
        if(!pmn1) {
            LogPrintf("CBznodeMan::ProcessVerifyBroadcast -- can't find bznode1 %s\n", mnv.vin1.prevout.ToStringShort());
            return;
        }

        CBznode* pmn2 = Find(mnv.vin2);
        if(!pmn2) {
            LogPrintf("CBznodeMan::ProcessVerifyBroadcast -- can't find bznode2 %s\n", mnv.vin2.prevout.ToStringShort());
            return;
        }

        if(pmn1->addr != mnv.addr) {
            LogPrintf("CBznodeMan::ProcessVerifyBroadcast -- addr %s do not match %s\n", mnv.addr.ToString(), pnode->addr.ToString());
            return;
        }

        if(darkSendSigner.VerifyMessage(pmn1->pubKeyBznode, mnv.vchSig1, strMessage1, strError)) {
            LogPrintf("BznodeMan::ProcessVerifyBroadcast -- VerifyMessage() for bznode1 failed, error: %s\n", strError);
            return;
        }

        if(darkSendSigner.VerifyMessage(pmn2->pubKeyBznode, mnv.vchSig2, strMessage2, strError)) {
            LogPrintf("BznodeMan::ProcessVerifyBroadcast -- VerifyMessage() for bznode2 failed, error: %s\n", strError);
            return;
        }

        if(!pmn1->IsPoSeVerified()) {
            pmn1->DecreasePoSeBanScore();
        }
        mnv.Relay();

        LogPrintf("CBznodeMan::ProcessVerifyBroadcast -- verified bznode %s for addr %s\n",
                    pmn1->vin.prevout.ToStringShort(), pnode->addr.ToString());

        // increase ban score for everyone else with the same addr
        int nCount = 0;
        BOOST_FOREACH(CBznode& mn, vBznodes) {
            if(mn.addr != mnv.addr || mn.vin.prevout == mnv.vin1.prevout) continue;
            mn.IncreasePoSeBanScore();
            nCount++;
            LogPrint("bznode", "CBznodeMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                        mn.vin.prevout.ToStringShort(), mn.addr.ToString(), mn.nPoSeBanScore);
        }
        LogPrintf("CBznodeMan::ProcessVerifyBroadcast -- PoSe score incresed for %d fake bznodes, addr %s\n",
                    nCount, pnode->addr.ToString());
    }
}

std::string CBznodeMan::ToString() const
{
    std::ostringstream info;

    info << "Bznodes: " << (int)vBznodes.size() <<
            ", peers who asked us for Bznode list: " << (int)mAskedUsForBznodeList.size() <<
            ", peers we asked for Bznode list: " << (int)mWeAskedForBznodeList.size() <<
            ", entries in Bznode list we asked for: " << (int)mWeAskedForBznodeListEntry.size() <<
            ", bznode index size: " << indexBznodes.GetSize() <<
            ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}

void CBznodeMan::UpdateBznodeList(CBznodeBroadcast mnb)
{
    try {
        LogPrintf("CBznodeMan::UpdateBznodeList\n");
        LOCK2(cs_main, cs);
        mapSeenBznodePing.insert(std::make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
        mapSeenBznodeBroadcast.insert(std::make_pair(mnb.GetHash(), std::make_pair(GetTime(), mnb)));

        LogPrintf("CBznodeMan::UpdateBznodeList -- bznode=%s  addr=%s\n", mnb.vin.prevout.ToStringShort(), mnb.addr.ToString());

        CBznode *pmn = Find(mnb.vin);
        if (pmn == NULL) {
            CBznode mn(mnb);
            if (Add(mn)) {
                bznodeSync.AddedBznodeList();
            }
        } else {
            CBznodeBroadcast mnbOld = mapSeenBznodeBroadcast[CBznodeBroadcast(*pmn).GetHash()].second;
            if (pmn->UpdateFromNewBroadcast(mnb)) {
                bznodeSync.AddedBznodeList();
                mapSeenBznodeBroadcast.erase(mnbOld.GetHash());
            }
        }
    } catch (const std::exception &e) {
        PrintExceptionContinue(&e, "UpdateBznodeList");
    }
}

bool CBznodeMan::CheckMnbAndUpdateBznodeList(CNode* pfrom, CBznodeBroadcast mnb, int& nDos)
{
    // Need LOCK2 here to ensure consistent locking order because the SimpleCheck call below locks cs_main
    LOCK(cs_main);

    {
        LOCK(cs);
        nDos = 0;
        LogPrint("bznode", "CBznodeMan::CheckMnbAndUpdateBznodeList -- bznode=%s\n", mnb.vin.prevout.ToStringShort());

        uint256 hash = mnb.GetHash();
        if (mapSeenBznodeBroadcast.count(hash) && !mnb.fRecovery) { //seen
            LogPrint("bznode", "CBznodeMan::CheckMnbAndUpdateBznodeList -- bznode=%s seen\n", mnb.vin.prevout.ToStringShort());
            // less then 2 pings left before this MN goes into non-recoverable state, bump sync timeout
            if (GetTime() - mapSeenBznodeBroadcast[hash].first > BZNODE_NEW_START_REQUIRED_SECONDS - BZNODE_MIN_MNP_SECONDS * 2) {
                LogPrint("bznode", "CBznodeMan::CheckMnbAndUpdateBznodeList -- bznode=%s seen update\n", mnb.vin.prevout.ToStringShort());
                mapSeenBznodeBroadcast[hash].first = GetTime();
                bznodeSync.AddedBznodeList();
            }
            // did we ask this node for it?
            if (pfrom && IsMnbRecoveryRequested(hash) && GetTime() < mMnbRecoveryRequests[hash].first) {
                LogPrint("bznode", "CBznodeMan::CheckMnbAndUpdateBznodeList -- mnb=%s seen request\n", hash.ToString());
                if (mMnbRecoveryRequests[hash].second.count(pfrom->addr)) {
                    LogPrint("bznode", "CBznodeMan::CheckMnbAndUpdateBznodeList -- mnb=%s seen request, addr=%s\n", hash.ToString(), pfrom->addr.ToString());
                    // do not allow node to send same mnb multiple times in recovery mode
                    mMnbRecoveryRequests[hash].second.erase(pfrom->addr);
                    // does it have newer lastPing?
                    if (mnb.lastPing.sigTime > mapSeenBznodeBroadcast[hash].second.lastPing.sigTime) {
                        // simulate Check
                        CBznode mnTemp = CBznode(mnb);
                        mnTemp.Check();
                        LogPrint("bznode", "CBznodeMan::CheckMnbAndUpdateBznodeList -- mnb=%s seen request, addr=%s, better lastPing: %d min ago, projected mn state: %s\n", hash.ToString(), pfrom->addr.ToString(), (GetTime() - mnb.lastPing.sigTime) / 60, mnTemp.GetStateString());
                        if (mnTemp.IsValidStateForAutoStart(mnTemp.nActiveState)) {
                            // this node thinks it's a good one
                            LogPrint("bznode", "CBznodeMan::CheckMnbAndUpdateBznodeList -- bznode=%s seen good\n", mnb.vin.prevout.ToStringShort());
                            mMnbRecoveryGoodReplies[hash].push_back(mnb);
                        }
                    }
                }
            }
            return true;
        }
        mapSeenBznodeBroadcast.insert(std::make_pair(hash, std::make_pair(GetTime(), mnb)));

        LogPrint("bznode", "CBznodeMan::CheckMnbAndUpdateBznodeList -- bznode=%s new\n", mnb.vin.prevout.ToStringShort());

        if (!mnb.SimpleCheck(nDos)) {
            LogPrint("bznode", "CBznodeMan::CheckMnbAndUpdateBznodeList -- SimpleCheck() failed, bznode=%s\n", mnb.vin.prevout.ToStringShort());
            return false;
        }

        // search Bznode list
        CBznode *pmn = Find(mnb.vin);
        if (pmn) {
            CBznodeBroadcast mnbOld = mapSeenBznodeBroadcast[CBznodeBroadcast(*pmn).GetHash()].second;
            if (!mnb.Update(pmn, nDos)) {
                LogPrint("bznode", "CBznodeMan::CheckMnbAndUpdateBznodeList -- Update() failed, bznode=%s\n", mnb.vin.prevout.ToStringShort());
                return false;
            }
            if (hash != mnbOld.GetHash()) {
                mapSeenBznodeBroadcast.erase(mnbOld.GetHash());
            }
        }
    } // end of LOCK(cs);

    if(mnb.CheckOutpoint(nDos)) {
        Add(mnb);
        bznodeSync.AddedBznodeList();
        // if it matches our Bznode privkey...
        if(fBZNode && mnb.pubKeyBznode == activeBznode.pubKeyBznode) {
            mnb.nPoSeBanScore = -BZNODE_POSE_BAN_MAX_SCORE;
            if(mnb.nProtocolVersion == PROTOCOL_VERSION) {
                // ... and PROTOCOL_VERSION, then we've been remotely activated ...
                LogPrintf("CBznodeMan::CheckMnbAndUpdateBznodeList -- Got NEW Bznode entry: bznode=%s  sigTime=%lld  addr=%s\n",
                            mnb.vin.prevout.ToStringShort(), mnb.sigTime, mnb.addr.ToString());
                activeBznode.ManageState();
            } else {
                // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
                // but also do not ban the node we get this message from
                LogPrintf("CBznodeMan::CheckMnbAndUpdateBznodeList -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", mnb.nProtocolVersion, PROTOCOL_VERSION);
                return false;
            }
        }
        mnb.RelayBZNode();
    } else {
        LogPrintf("CBznodeMan::CheckMnbAndUpdateBznodeList -- Rejected Bznode entry: %s  addr=%s\n", mnb.vin.prevout.ToStringShort(), mnb.addr.ToString());
        return false;
    }

    return true;
}

void CBznodeMan::UpdateLastPaid()
{
    LOCK(cs);
    if(fLiteMode) return;
    if(!pCurrentBlockIndex) {
        // LogPrintf("CBznodeMan::UpdateLastPaid, pCurrentBlockIndex=NULL\n");
        return;
    }

    static bool IsFirstRun = true;
    // Do full scan on first run or if we are not a bznode
    // (MNs should update this info on every block, so limited scan should be enough for them)
    int nMaxBlocksToScanBack = (IsFirstRun || !fBZNode) ? mnpayments.GetStorageLimit() : LAST_PAID_SCAN_BLOCKS;

    LogPrint("mnpayments", "CBznodeMan::UpdateLastPaid -- nHeight=%d, nMaxBlocksToScanBack=%d, IsFirstRun=%s\n",
                             pCurrentBlockIndex->nHeight, nMaxBlocksToScanBack, IsFirstRun ? "true" : "false");

    BOOST_FOREACH(CBznode& mn, vBznodes) {
        mn.UpdateLastPaid(pCurrentBlockIndex, nMaxBlocksToScanBack);
    }

    // every time is like the first time if winners list is not synced
    IsFirstRun = !bznodeSync.IsWinnersListSynced();
}

void CBznodeMan::CheckAndRebuildBznodeIndex()
{
    LOCK(cs);

    if(GetTime() - nLastIndexRebuildTime < MIN_INDEX_REBUILD_TIME) {
        return;
    }

    if(indexBznodes.GetSize() <= MAX_EXPECTED_INDEX_SIZE) {
        return;
    }

    if(indexBznodes.GetSize() <= int(vBznodes.size())) {
        return;
    }

    indexBznodesOld = indexBznodes;
    indexBznodes.Clear();
    for(size_t i = 0; i < vBznodes.size(); ++i) {
        indexBznodes.AddBznodeVIN(vBznodes[i].vin);
    }

    fIndexRebuilt = true;
    nLastIndexRebuildTime = GetTime();
}

void CBznodeMan::UpdateWatchdogVoteTime(const CTxIn& vin)
{
    LOCK(cs);
    CBznode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->UpdateWatchdogVoteTime();
    nLastWatchdogVoteTime = GetTime();
}

bool CBznodeMan::IsWatchdogActive()
{
    LOCK(cs);
    // Check if any bznodes have voted recently, otherwise return false
    return (GetTime() - nLastWatchdogVoteTime) <= BZNODE_WATCHDOG_MAX_SECONDS;
}

void CBznodeMan::CheckBznode(const CTxIn& vin, bool fForce)
{
    LOCK(cs);
    CBznode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->Check(fForce);
}

void CBznodeMan::CheckBznode(const CPubKey& pubKeyBznode, bool fForce)
{
    LOCK(cs);
    CBznode* pMN = Find(pubKeyBznode);
    if(!pMN)  {
        return;
    }
    pMN->Check(fForce);
}

int CBznodeMan::GetBznodeState(const CTxIn& vin)
{
    LOCK(cs);
    CBznode* pMN = Find(vin);
    if(!pMN)  {
        return CBznode::BZNODE_NEW_START_REQUIRED;
    }
    return pMN->nActiveState;
}

int CBznodeMan::GetBznodeState(const CPubKey& pubKeyBznode)
{
    LOCK(cs);
    CBznode* pMN = Find(pubKeyBznode);
    if(!pMN)  {
        return CBznode::BZNODE_NEW_START_REQUIRED;
    }
    return pMN->nActiveState;
}

bool CBznodeMan::IsBznodePingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt)
{
    LOCK(cs);
    CBznode* pMN = Find(vin);
    if(!pMN) {
        return false;
    }
    return pMN->IsPingedWithin(nSeconds, nTimeToCheckAt);
}

void CBznodeMan::SetBznodeLastPing(const CTxIn& vin, const CBznodePing& mnp)
{
    LOCK(cs);
    CBznode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->lastPing = mnp;
    mapSeenBznodePing.insert(std::make_pair(mnp.GetHash(), mnp));

    CBznodeBroadcast mnb(*pMN);
    uint256 hash = mnb.GetHash();
    if(mapSeenBznodeBroadcast.count(hash)) {
        mapSeenBznodeBroadcast[hash].second.lastPing = mnp;
    }
}

void CBznodeMan::UpdatedBlockTip(const CBlockIndex *pindex)
{
    pCurrentBlockIndex = pindex;
    LogPrint("bznode", "CBznodeMan::UpdatedBlockTip -- pCurrentBlockIndex->nHeight=%d\n", pCurrentBlockIndex->nHeight);

    CheckSameAddr();

    if(fBZNode) {
        // normal wallet does not need to update this every block, doing update on rpc call should be enough
        UpdateLastPaid();
    }
}

void CBznodeMan::NotifyBznodeUpdates()
{
    // Avoid double locking
    bool fBznodesAddedLocal = false;
    bool fBznodesRemovedLocal = false;
    {
        LOCK(cs);
        fBznodesAddedLocal = fBznodesAdded;
        fBznodesRemovedLocal = fBznodesRemoved;
    }

    if(fBznodesAddedLocal) {
//        governance.CheckBznodeOrphanObjects();
//        governance.CheckBznodeOrphanVotes();
    }
    if(fBznodesRemovedLocal) {
//        governance.UpdateCachesAndClean();
    }

    LOCK(cs);
    fBznodesAdded = false;
    fBznodesRemoved = false;
}
