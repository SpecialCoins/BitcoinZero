// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activebznode.h"
#include "darksend.h"
#include "bznode-payments.h"
#include "bznode-sync.h"
#include "bznodeman.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "util.h"
#include "consensus/consensus.h"

#include <boost/lexical_cast.hpp>

/** Object for who's going to get paid on which blocks */
CBznodePayments mnpayments;

CCriticalSection cs_vecPayees;
CCriticalSection cs_mapBznodeBlocks;
CCriticalSection cs_mapBznodePaymentVotes;

bool IsBlockValueValid(const CBlock &block, int nBlockHeight, CAmount blockReward) {

    bool isBlockRewardValueMet = (block.vtx[0].GetValueOut() <= blockReward);
    return isBlockRewardValueMet;
}

bool IsBlockPayeeValid(const CTransaction &txNew, int nBlockHeight, CAmount blockReward) {

    if (!sporkManager.IsSporkActive(SPORK_14_BZNODE_PAYMENT_START)) {
        //there is no data to use to check anything, let's just accept the longest chain
        if (fDebug) LogPrintf("IsBlockPayeeValid -- bznode not started\n");
        return true;
    }
    if (!bznodeSync.IsSynced()) {
        //there is no data to use to check anything, let's just accept the longest chain
        if (fDebug) LogPrintf("IsBlockPayeeValid -- WARNING: Client not synced, skipping block payee checks\n");
        return true;
    }

    //check for bznode payee
    if (mnpayments.IsTransactionValid(txNew, nBlockHeight)) {
        return true;
    } else {
        if (sporkManager.IsSporkActive(SPORK_15_BZNODE_PAYMENT_ENFORCEMENT) && bznodeSync.IsSynced()) {
            return false;
        } else {
            LogPrintf("BZNode payment enforcement is disabled, accepting block\n");
            return true;
        }
    }
}

void FillBlockPayments(CMutableTransaction &txNew, int nBlockHeight, CAmount bznodePayment, CTxOut &txoutBznodeRet) {

    // FILL BLOCK PAYEE WITH BZNODE PAYMENT OTHERWISE
    mnpayments.FillBlockPayee(txNew, nBlockHeight, bznodePayment, txoutBznodeRet);
    LogPrint("mnpayments", "FillBlockPayments -- nBlockHeight %d bznodePayment %lld txoutBznodeRet %s txNew %s",
             nBlockHeight, bznodePayment, txoutBznodeRet.ToString(), txNew.ToString());
}

std::string GetRequiredPaymentsString(int nBlockHeight) {

    return mnpayments.GetRequiredPaymentsString(nBlockHeight);
}

void CBznodePayments::Clear() {
    LOCK2(cs_mapBznodeBlocks, cs_mapBznodePaymentVotes);
    mapBznodeBlocks.clear();
    mapBznodePaymentVotes.clear();
}

bool CBznodePayments::CanVote(COutPoint outBznode, int nBlockHeight) {
    LOCK(cs_mapBznodePaymentVotes);

    if (mapBznodesLastVote.count(outBznode) && mapBznodesLastVote[outBznode] == nBlockHeight) {
        return false;
    }

    //record this bznode voted
    mapBznodesLastVote[outBznode] = nBlockHeight;
    return true;
}

std::string CBznodePayee::ToString() const {
    CTxDestination address1;
    ExtractDestination(scriptPubKey, address1);
    CBitcoinAddress address2(address1);
    std::string str;
    str += "(address: ";
    str += address2.ToString();
    str += ")\n";
    return str;
}

/**
*   FillBlockPayee
*
*   Fill Bznode ONLY payment block
*/

void CBznodePayments::FillBlockPayee(CMutableTransaction &txNew, int nBlockHeight, CAmount bznodePayment, CTxOut &txoutBznodeRet) {
    // make sure it's not filled yet
    txoutBznodeRet = CTxOut();

    CScript payee;
    bool foundMaxVotedPayee = true;

    if (!mnpayments.GetBlockPayee(nBlockHeight, payee)) {
        // no bznode detected...
        // LogPrintf("no bznode detected...\n");
        foundMaxVotedPayee = false;
        int nCount = 0;
        CBznode *winningNode = mnodeman.GetNextBznodeInQueueForPayment(nBlockHeight, true, nCount);
        if (!winningNode) {
            if(Params().NetworkIDString() != CBaseChainParams::REGTEST) {
                // ...and we can't calculate it on our own
                LogPrintf("CBznodePayments::FillBlockPayee -- Failed to detect bznode to pay\n");
                return;
            }
        }
        // fill payee with locally calculated winner and hope for the best
        if (winningNode) {
            payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
            LogPrintf("payee=%s\n", winningNode->ToString());
        }
        else
            payee = txNew.vout[0].scriptPubKey;//This is only for unit tests scenario on REGTEST
    }
    txoutBznodeRet = CTxOut(bznodePayment, payee);
    txNew.vout.push_back(txoutBznodeRet);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);
    if (foundMaxVotedPayee) {
        LogPrintf("CBznodePayments::FillBlockPayee::foundMaxVotedPayee -- Bznode payment %lld to %s\n", bznodePayment, address2.ToString());
    } else {
        LogPrintf("CBznodePayments::FillBlockPayee -- Bznode payment %lld to %s\n", bznodePayment, address2.ToString());
    }

}

int CBznodePayments::GetMinBznodePaymentsProto() {

    int minProtocol = (sporkManager.IsSporkActive(SPORK_4_VERSION_ON)) ? PROTOCOL_VERSION : MIN_PEER_PROTO_VERSION;
    return minProtocol;
}

void CBznodePayments::ProcessMessage(CNode *pfrom, std::string &strCommand, CDataStream &vRecv) {

//    LogPrintf("CBznodePayments::ProcessMessage strCommand=%s\n", strCommand);
    // Ignore any payments messages until bznode list is synced
    if (!bznodeSync.IsBznodeListSynced()) return;

    if (fLiteMode) return; // disable all BitcoinZero specific functionality

    bool fTestNet = (Params().NetworkIDString() == CBaseChainParams::TESTNET);

    if (strCommand == NetMsgType::BZNODEPAYMENTSYNC) { //Bznode Payments Request Sync

        // Ignore such requests until we are fully synced.
        // We could start processing this after bznode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!bznodeSync.IsSynced()) return;

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (netfulfilledman.HasFulfilledRequest(pfrom->addr, NetMsgType::BZNODEPAYMENTSYNC)) {
            // Asking for the payments list multiple times in a short period of time is no good
            LogPrintf("BZNODEPAYMENTSYNC -- peer already asked me for the list, peer=%d\n", pfrom->id);
            if (!fTestNet) Misbehaving(pfrom->GetId(), 20);
            return;
        }
        netfulfilledman.AddFulfilledRequest(pfrom->addr, NetMsgType::BZNODEPAYMENTSYNC);

        Sync(pfrom);
        LogPrint("mnpayments", "BZNODEPAYMENTSYNC -- Sent Bznode payment votes to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::BZNODEPAYMENTVOTE) { // Bznode Payments Vote for the Winner

        CBznodePaymentVote vote;
        vRecv >> vote;

        if (pfrom->nVersion < GetMinBznodePaymentsProto()) return;

        if (!pCurrentBlockIndex) return;

        uint256 nHash = vote.GetHash();

        pfrom->setAskFor.erase(nHash);

        {
            LOCK(cs_mapBznodePaymentVotes);
            if (mapBznodePaymentVotes.count(nHash)) {
                LogPrint("mnpayments", "BZNODEPAYMENTVOTE -- hash=%s, nHeight=%d seen\n", nHash.ToString(), pCurrentBlockIndex->nHeight);
                return;
            }

            // Avoid processing same vote multiple times
            mapBznodePaymentVotes[nHash] = vote;
            // but first mark vote as non-verified,
            // AddPaymentVote() below should take care of it if vote is actually ok
            mapBznodePaymentVotes[nHash].MarkAsNotVerified();
        }

        int nFirstBlock = pCurrentBlockIndex->nHeight - GetStorageLimit();
        if (vote.nBlockHeight < nFirstBlock || vote.nBlockHeight > pCurrentBlockIndex->nHeight + 20) {
            LogPrint("mnpayments", "BZNODEPAYMENTVOTE -- vote out of range: nFirstBlock=%d, nBlockHeight=%d, nHeight=%d\n", nFirstBlock, vote.nBlockHeight, pCurrentBlockIndex->nHeight);
            return;
        }

        std::string strError = "";
        if (!vote.IsValid(pfrom, pCurrentBlockIndex->nHeight, strError)) {
            LogPrint("mnpayments", "BZNODEPAYMENTVOTE -- invalid message, error: %s\n", strError);
            return;
        }

        if (!CanVote(vote.vinBznode.prevout, vote.nBlockHeight)) {
            LogPrintf("BZNODEPAYMENTVOTE -- bznode already voted, bznode=%s\n", vote.vinBznode.prevout.ToStringShort());
            return;
        }

        bznode_info_t mnInfo = mnodeman.GetBznodeInfo(vote.vinBznode);
        if (!mnInfo.fInfoValid) {
            // mn was not found, so we can't check vote, some info is probably missing
            LogPrintf("BZNODEPAYMENTVOTE -- bznode is missing %s\n", vote.vinBznode.prevout.ToStringShort());
            mnodeman.AskForMN(pfrom, vote.vinBznode);
            return;
        }

        int nDos = 0;
        if (!vote.CheckSignature(mnInfo.pubKeyBznode, pCurrentBlockIndex->nHeight, nDos)) {
            if (nDos) {
                LogPrintf("BZNODEPAYMENTVOTE -- ERROR: invalid signature\n");
                if (!fTestNet) Misbehaving(pfrom->GetId(), nDos);
            } else {
                // only warn about anything non-critical (i.e. nDos == 0) in debug mode
                LogPrint("mnpayments", "BZNODEPAYMENTVOTE -- WARNING: invalid signature\n");
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update,
            mnodeman.AskForMN(pfrom, vote.vinBznode);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a mn which changed its key),
            // so just quit here.
            return;
        }

        CTxDestination address1;
        ExtractDestination(vote.payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("mnpayments", "BZNODEPAYMENTVOTE -- vote: address=%s, nBlockHeight=%d, nHeight=%d, prevout=%s\n", address2.ToString(), vote.nBlockHeight, pCurrentBlockIndex->nHeight, vote.vinBznode.prevout.ToStringShort());

        if (AddPaymentVote(vote)) {
            vote.Relay();
            bznodeSync.AddedPaymentVote();
        }
    }
}

bool CBznodePaymentVote::Sign() {
    std::string strError;
    std::string strMessage = vinBznode.prevout.ToStringShort() +
                             boost::lexical_cast<std::string>(nBlockHeight) +
                             ScriptToAsmStr(payee);

    if (!darkSendSigner.SignMessage(strMessage, vchSig, activeBznode.keyBznode)) {
        LogPrintf("CBznodePaymentVote::Sign -- SignMessage() failed\n");
        return false;
    }

    if (!darkSendSigner.VerifyMessage(activeBznode.pubKeyBznode, vchSig, strMessage, strError)) {
        LogPrintf("CBznodePaymentVote::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CBznodePayments::GetBlockPayee(int nBlockHeight, CScript &payee) {
    if (mapBznodeBlocks.count(nBlockHeight)) {
        return mapBznodeBlocks[nBlockHeight].GetBestPayee(payee);
    }

    return false;
}

// Is this bznode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 blocks of votes
bool CBznodePayments::IsScheduled(CBznode &mn, int nNotBlockHeight) {
    LOCK(cs_mapBznodeBlocks);

    if (!pCurrentBlockIndex) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for (int64_t h = pCurrentBlockIndex->nHeight; h <= pCurrentBlockIndex->nHeight + 8; h++) {
        if (h == nNotBlockHeight) continue;
        if (mapBznodeBlocks.count(h) && mapBznodeBlocks[h].GetBestPayee(payee) && mnpayee == payee) {
            return true;
        }
    }

    return false;
}

bool CBznodePayments::AddPaymentVote(const CBznodePaymentVote &vote) {
    LogPrint("bznode-payments", "CBznodePayments::AddPaymentVote\n");
    uint256 blockHash = uint256();
    if (!GetBlockHash(blockHash, vote.nBlockHeight - 101)) return false;

    if (HasVerifiedPaymentVote(vote.GetHash())) return false;

    LOCK2(cs_mapBznodeBlocks, cs_mapBznodePaymentVotes);

    mapBznodePaymentVotes[vote.GetHash()] = vote;

    if (!mapBznodeBlocks.count(vote.nBlockHeight)) {
        CBznodeBlockPayees blockPayees(vote.nBlockHeight);
        mapBznodeBlocks[vote.nBlockHeight] = blockPayees;
    }

    mapBznodeBlocks[vote.nBlockHeight].AddPayee(vote);

    return true;
}

bool CBznodePayments::HasVerifiedPaymentVote(uint256 hashIn) {
    LOCK(cs_mapBznodePaymentVotes);
    std::map<uint256, CBznodePaymentVote>::iterator it = mapBznodePaymentVotes.find(hashIn);
    return it != mapBznodePaymentVotes.end() && it->second.IsVerified();
}

void CBznodeBlockPayees::AddPayee(const CBznodePaymentVote &vote) {
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CBznodePayee & payee, vecPayees)
    {
        if (payee.GetPayee() == vote.payee) {
            payee.AddVoteHash(vote.GetHash());
            return;
        }
    }
    CBznodePayee payeeNew(vote.payee, vote.GetHash());
    vecPayees.push_back(payeeNew);
}

bool CBznodeBlockPayees::GetBestPayee(CScript &payeeRet) {
    LOCK(cs_vecPayees);
    LogPrint("mnpayments", "CBznodeBlockPayees::GetBestPayee, vecPayees.size()=%s\n", vecPayees.size());
    if (!vecPayees.size()) {
        LogPrint("mnpayments", "CBznodeBlockPayees::GetBestPayee -- ERROR: couldn't find any payee\n");
        return false;
    }

    int nVotes = -1;
    BOOST_FOREACH(CBznodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() > nVotes) {
            payeeRet = payee.GetPayee();
            nVotes = payee.GetVoteCount();
        }
    }

    return (nVotes > -1);
}

bool CBznodeBlockPayees::HasPayeeWithVotes(CScript payeeIn, int nVotesReq) {
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CBznodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() >= nVotesReq && payee.GetPayee() == payeeIn) {
            return true;
        }
    }

//    LogPrint("mnpayments", "CBznodeBlockPayees::HasPayeeWithVotes -- ERROR: couldn't find any payee with %d+ votes\n", nVotesReq);
    return false;
}

bool CBznodeBlockPayees::IsTransactionValid(const CTransaction &txNew) {
    LOCK(cs_vecPayees);

    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";


    CAmount nBznodePayment = GetBznodePayment(nBlockHeight);

    //require at least MNPAYMENTS_SIGNATURES_REQUIRED signatures

    BOOST_FOREACH(CBznodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() >= nMaxSignatures) {
            nMaxSignatures = payee.GetVoteCount();
        }
    }

    // if we don't have at least MNPAYMENTS_SIGNATURES_REQUIRED signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    bool hasValidPayee = false;

    BOOST_FOREACH(CBznodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            hasValidPayee = true;

            BOOST_FOREACH(CTxOut txout, txNew.vout) {
                if (payee.GetPayee() == txout.scriptPubKey && nBznodePayment == txout.nValue) {
                    LogPrint("mnpayments", "CBznodeBlockPayees::IsTransactionValid -- Found required payment\n");
                    return true;
                }
            }

            CTxDestination address1;
            ExtractDestination(payee.GetPayee(), address1);
            CBitcoinAddress address2(address1);

            if (strPayeesPossible == "") {
                strPayeesPossible = address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    if (!hasValidPayee) return true;

    LogPrintf("CBznodeBlockPayees::IsTransactionValid -- ERROR: Missing required payment, possible payees: '%s', amount: %f BZX\n", strPayeesPossible, (float) nBznodePayment / COIN);
    return false;
}

std::string CBznodeBlockPayees::GetRequiredPaymentsString() {
    LOCK(cs_vecPayees);

    std::string strRequiredPayments = "Unknown";

    BOOST_FOREACH(CBznodePayee & payee, vecPayees)
    {
        CTxDestination address1;
        ExtractDestination(payee.GetPayee(), address1);
        CBitcoinAddress address2(address1);

        if (strRequiredPayments != "Unknown") {
            strRequiredPayments += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        } else {
            strRequiredPayments = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        }
    }

    return strRequiredPayments;
}

std::string CBznodePayments::GetRequiredPaymentsString(int nBlockHeight) {
    LOCK(cs_mapBznodeBlocks);

    if (mapBznodeBlocks.count(nBlockHeight)) {
        return mapBznodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CBznodePayments::IsTransactionValid(const CTransaction &txNew, int nBlockHeight) {
    LOCK(cs_mapBznodeBlocks);

    if (mapBznodeBlocks.count(nBlockHeight)) {
        return mapBznodeBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CBznodePayments::CheckAndRemove() {
    if (!pCurrentBlockIndex) return;

    LOCK2(cs_mapBznodeBlocks, cs_mapBznodePaymentVotes);

    int nLimit = GetStorageLimit();

    std::map<uint256, CBznodePaymentVote>::iterator it = mapBznodePaymentVotes.begin();
    while (it != mapBznodePaymentVotes.end()) {
        CBznodePaymentVote vote = (*it).second;

        if (pCurrentBlockIndex->nHeight - vote.nBlockHeight > nLimit) {
            LogPrint("mnpayments", "CBznodePayments::CheckAndRemove -- Removing old Bznode payment: nBlockHeight=%d\n", vote.nBlockHeight);
            mapBznodePaymentVotes.erase(it++);
            mapBznodeBlocks.erase(vote.nBlockHeight);
        } else {
            ++it;
        }
    }
    LogPrintf("CBznodePayments::CheckAndRemove -- %s\n", ToString());
}

bool CBznodePaymentVote::IsValid(CNode *pnode, int nValidationHeight, std::string &strError) {

    CBznode *pmn = mnodeman.Find(vinBznode);

    if (!pmn) {
        strError = strprintf("Unknown Bznode: prevout=%s", vinBznode.prevout.ToStringShort());
        // Only ask if we are already synced and still have no idea about that Bznode
        if (bznodeSync.IsBznodeListSynced()) {
            mnodeman.AskForMN(pnode, vinBznode);
        }

        return false;
    }

    int nMinRequiredProtocol;
    nMinRequiredProtocol = mnpayments.GetMinBznodePaymentsProto();

    if (pmn->nProtocolVersion < nMinRequiredProtocol) {
        strError = strprintf("Bznode protocol is too old: nProtocolVersion=%d, nMinRequiredProtocol=%d", pmn->nProtocolVersion, nMinRequiredProtocol);
        return false;
    }

    // Only bznodes should try to check bznode rank for old votes - they need to pick the right winner for future blocks.
    // Regular clients (miners included) need to verify bznode rank for future block votes only.
    if (!fBZNode && nBlockHeight < nValidationHeight) return true;

    int nRank = mnodeman.GetBznodeRank(vinBznode, nBlockHeight - 101, nMinRequiredProtocol, false);

    if (nRank == -1) {
        LogPrint("mnpayments", "CBznodePaymentVote::IsValid -- Can't calculate rank for bznode %s\n",
                 vinBznode.prevout.ToStringShort());
        return false;
    }

    if (nRank > MNPAYMENTS_SIGNATURES_TOTAL) {
        // It's common to have bznodes mistakenly think they are in the top 10
        // We don't want to print all of these messages in normal mode, debug mode should print though
        strError = strprintf("Bznode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        // Only ban for new mnw which is out of bounds, for old mnw MN list itself might be way too much off
        if (nRank > MNPAYMENTS_SIGNATURES_TOTAL * 2 && nBlockHeight > nValidationHeight) {
            strError = strprintf("Bznode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL * 2, nRank);
            LogPrintf("CBznodePaymentVote::IsValid -- Error: %s\n", strError);
            Misbehaving(pnode->GetId(), 20);
        }
        // Still invalid however
        return false;
    }

    return true;
}

bool CBznodePayments::ProcessBlock(int nBlockHeight) {

    // DETERMINE IF WE SHOULD BE VOTING FOR THE NEXT PAYEE

    if (fLiteMode || !fBZNode) {
        return false;
    }

    // We have little chances to pick the right winner if winners list is out of sync
    // but we have no choice, so we'll try. However it doesn't make sense to even try to do so
    // if we have not enough data about bznodes.
    if (!bznodeSync.IsBznodeListSynced()) {
        return false;
    }

    int nRank = mnodeman.GetBznodeRank(activeBznode.vin, nBlockHeight - 101, GetMinBznodePaymentsProto(), false);

    if (nRank == -1) {
        LogPrint("mnpayments", "CBznodePayments::ProcessBlock -- Unknown Bznode\n");
        return false;
    }

    if (nRank > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CBznodePayments::ProcessBlock -- Bznode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        return false;
    }

    // LOCATE THE NEXT BZNODE WHICH SHOULD BE PAID

    LogPrintf("CBznodePayments::ProcessBlock -- Start: nBlockHeight=%d, bznode=%s\n", nBlockHeight, activeBznode.vin.prevout.ToStringShort());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    CBznode *pmn = mnodeman.GetNextBznodeInQueueForPayment(nBlockHeight, true, nCount);

    if (pmn == NULL) {
        LogPrintf("CBznodePayments::ProcessBlock -- ERROR: Failed to find bznode to pay\n");
        return false;
    }

    LogPrintf("CBznodePayments::ProcessBlock -- Bznode found by GetNextBznodeInQueueForPayment(): %s\n", pmn->vin.prevout.ToStringShort());


    CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());

    CBznodePaymentVote voteNew(activeBznode.vin, nBlockHeight, payee);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);

    // SIGN MESSAGE TO NETWORK WITH OUR BZNODE KEYS

    if (voteNew.Sign()) {
        if (AddPaymentVote(voteNew)) {
            voteNew.Relay();
            return true;
        }
    }

    return false;
}

void CBznodePaymentVote::Relay() {
    // do not relay until synced
    if (!bznodeSync.IsWinnersListSynced()) {
        LogPrint("bznode", "CBznodePaymentVote::Relay - bznodeSync.IsWinnersListSynced() not sync\n");
        return;
    }
    CInv inv(MSG_BZNODE_PAYMENT_VOTE, GetHash());
    RelayInv(inv);
}

bool CBznodePaymentVote::CheckSignature(const CPubKey &pubKeyBznode, int nValidationHeight, int &nDos) {
    // do not ban by default
    nDos = 0;

    std::string strMessage = vinBznode.prevout.ToStringShort() +
                             boost::lexical_cast<std::string>(nBlockHeight) +
                             ScriptToAsmStr(payee);

    std::string strError = "";
    if (!darkSendSigner.VerifyMessage(pubKeyBznode, vchSig, strMessage, strError)) {
        // Only ban for future block vote when we are already synced.
        // Otherwise it could be the case when MN which signed this vote is using another key now
        // and we have no idea about the old one.
        if (bznodeSync.IsBznodeListSynced() && nBlockHeight > nValidationHeight) {
            nDos = 20;
        }
        return error("CBznodePaymentVote::CheckSignature -- Got bad Bznode payment signature, bznode=%s, error: %s", vinBznode.prevout.ToStringShort().c_str(), strError);
    }

    return true;
}

std::string CBznodePaymentVote::ToString() const {
    std::ostringstream info;

    info << vinBznode.prevout.ToStringShort() <<
         ", " << nBlockHeight <<
         ", " << ScriptToAsmStr(payee) <<
         ", " << (int) vchSig.size();

    return info.str();
}

// Send only votes for future blocks, node should request every other missing payment block individually
void CBznodePayments::Sync(CNode *pnode) {
    LOCK(cs_mapBznodeBlocks);

    if (!pCurrentBlockIndex) return;

    int nInvCount = 0;

    for (int h = pCurrentBlockIndex->nHeight; h < pCurrentBlockIndex->nHeight + 20; h++) {
        if (mapBznodeBlocks.count(h)) {
            BOOST_FOREACH(CBznodePayee & payee, mapBznodeBlocks[h].vecPayees)
            {
                std::vector <uint256> vecVoteHashes = payee.GetVoteHashes();
                BOOST_FOREACH(uint256 & hash, vecVoteHashes)
                {
                    if (!HasVerifiedPaymentVote(hash)) continue;
                    pnode->PushInventory(CInv(MSG_BZNODE_PAYMENT_VOTE, hash));
                    nInvCount++;
                }
            }
        }
    }

    LogPrintf("CBznodePayments::Sync -- Sent %d votes to peer %d\n", nInvCount, pnode->id);
    pnode->PushMessage(NetMsgType::SYNCSTATUSCOUNT, BZNODE_SYNC_MNW, nInvCount);
}

// Request low data/unknown payment blocks in batches directly from some node instead of/after preliminary Sync.
void CBznodePayments::RequestLowDataPaymentBlocks(CNode *pnode) {
    if (!pCurrentBlockIndex) return;

    LOCK2(cs_main, cs_mapBznodeBlocks);

    std::vector <CInv> vToFetch;
    int nLimit = GetStorageLimit();

    const CBlockIndex *pindex = pCurrentBlockIndex;

    while (pCurrentBlockIndex->nHeight - pindex->nHeight < nLimit) {
        if (!mapBznodeBlocks.count(pindex->nHeight)) {
            // We have no idea about this block height, let's ask
            vToFetch.push_back(CInv(MSG_BZNODE_PAYMENT_BLOCK, pindex->GetBlockHash()));
            // We should not violate GETDATA rules
            if (vToFetch.size() == MAX_INV_SZ) {
                LogPrintf("CBznodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d blocks\n", pnode->id, MAX_INV_SZ);
                pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
                // Start filling new batch
                vToFetch.clear();
            }
        }
        if (!pindex->pprev) break;
        pindex = pindex->pprev;
    }

    std::map<int, CBznodeBlockPayees>::iterator it = mapBznodeBlocks.begin();

    while (it != mapBznodeBlocks.end()) {
        int nTotalVotes = 0;
        bool fFound = false;
        BOOST_FOREACH(CBznodePayee & payee, it->second.vecPayees)
        {
            if (payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED) {
                fFound = true;
                break;
            }
            nTotalVotes += payee.GetVoteCount();
        }
        // A clear winner (MNPAYMENTS_SIGNATURES_REQUIRED+ votes) was found
        // or no clear winner was found but there are at least avg number of votes
        if (fFound || nTotalVotes >= (MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED) / 2) {
            // so just move to the next block
            ++it;
            continue;
        }
        // DEBUG
//        DBG (
//            // Let's see why this failed
//            BOOST_FOREACH(CBznodePayee& payee, it->second.vecPayees) {
//                CTxDestination address1;
//                ExtractDestination(payee.GetPayee(), address1);
//                CBitcoinAddress address2(address1);
//                printf("payee %s votes %d\n", address2.ToString().c_str(), payee.GetVoteCount());
//            }
//            printf("block %d votes total %d\n", it->first, nTotalVotes);
//        )
        // END DEBUG
        // Low data block found, let's try to sync it
        uint256 hash;
        if (GetBlockHash(hash, it->first)) {
            vToFetch.push_back(CInv(MSG_BZNODE_PAYMENT_BLOCK, hash));
        }
        // We should not violate GETDATA rules
        if (vToFetch.size() == MAX_INV_SZ) {
            LogPrintf("CBznodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, MAX_INV_SZ);
            pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
            // Start filling new batch
            vToFetch.clear();
        }
        ++it;
    }
    // Ask for the rest of it
    if (!vToFetch.empty()) {
        LogPrintf("CBznodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, vToFetch.size());
        pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
    }
}

std::string CBznodePayments::ToString() const {
    std::ostringstream info;

    info << "Votes: " << (int) mapBznodePaymentVotes.size() <<
         ", Blocks: " << (int) mapBznodeBlocks.size();

    return info.str();
}

bool CBznodePayments::IsEnoughData() {
    float nAverageVotes = (MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED) / 2;
    int nStorageLimit = GetStorageLimit();
    return GetBlockCount() > nStorageLimit && GetVoteCount() > nStorageLimit * nAverageVotes;
}

int CBznodePayments::GetStorageLimit() {
    return std::max(int(mnodeman.size() * nStorageCoeff), nMinBlocksToStore);
}

void CBznodePayments::UpdatedBlockTip(const CBlockIndex *pindex) {
    pCurrentBlockIndex = pindex;
    LogPrint("mnpayments", "CBznodePayments::UpdatedBlockTip -- pCurrentBlockIndex->nHeight=%d\n", pCurrentBlockIndex->nHeight);
    
    ProcessBlock(pindex->nHeight + 5);
}
