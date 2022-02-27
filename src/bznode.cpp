// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activebznode.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "darksend.h"
#include "init.h"
//#include "governance.h"
#include "bznode.h"
#include "bznode-payments.h"
#include "bznode-sync.h"
#include "bznodeman.h"
#include "util.h"

#include <boost/lexical_cast.hpp>


CBznode::CBznode() :
        vin(),
        addr(),
        pubKeyCollateralAddress(),
        pubKeyBznode(),
        lastPing(),
        vchSig(),
        sigTime(GetAdjustedTime()),
        nLastDsq(0),
        nTimeLastChecked(0),
        nTimeLastPaid(0),
        nTimeLastWatchdogVote(0),
        nActiveState(BZNODE_ENABLED),
        nCacheCollateralBlock(0),
        nBlockLastPaid(0),
        nProtocolVersion(PROTOCOL_VERSION),
        nPoSeBanScore(0),
        nPoSeBanHeight(0),
        fAllowMixingTx(true),
        fUnitTest(false) {}

CBznode::CBznode(CService addrNew, CTxIn vinNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyBznodeNew, int nProtocolVersionIn) :
        vin(vinNew),
        addr(addrNew),
        pubKeyCollateralAddress(pubKeyCollateralAddressNew),
        pubKeyBznode(pubKeyBznodeNew),
        lastPing(),
        vchSig(),
        sigTime(GetAdjustedTime()),
        nLastDsq(0),
        nTimeLastChecked(0),
        nTimeLastPaid(0),
        nTimeLastWatchdogVote(0),
        nActiveState(BZNODE_ENABLED),
        nCacheCollateralBlock(0),
        nBlockLastPaid(0),
        nProtocolVersion(nProtocolVersionIn),
        nPoSeBanScore(0),
        nPoSeBanHeight(0),
        fAllowMixingTx(true),
        fUnitTest(false) {}

CBznode::CBznode(const CBznode &other) :
        vin(other.vin),
        addr(other.addr),
        pubKeyCollateralAddress(other.pubKeyCollateralAddress),
        pubKeyBznode(other.pubKeyBznode),
        lastPing(other.lastPing),
        vchSig(other.vchSig),
        sigTime(other.sigTime),
        nLastDsq(other.nLastDsq),
        nTimeLastChecked(other.nTimeLastChecked),
        nTimeLastPaid(other.nTimeLastPaid),
        nTimeLastWatchdogVote(other.nTimeLastWatchdogVote),
        nActiveState(other.nActiveState),
        nCacheCollateralBlock(other.nCacheCollateralBlock),
        nBlockLastPaid(other.nBlockLastPaid),
        nProtocolVersion(other.nProtocolVersion),
        nPoSeBanScore(other.nPoSeBanScore),
        nPoSeBanHeight(other.nPoSeBanHeight),
        fAllowMixingTx(other.fAllowMixingTx),
        fUnitTest(other.fUnitTest) {}

CBznode::CBznode(const CBznodeBroadcast &mnb) :
        vin(mnb.vin),
        addr(mnb.addr),
        pubKeyCollateralAddress(mnb.pubKeyCollateralAddress),
        pubKeyBznode(mnb.pubKeyBznode),
        lastPing(mnb.lastPing),
        vchSig(mnb.vchSig),
        sigTime(mnb.sigTime),
        nLastDsq(0),
        nTimeLastChecked(0),
        nTimeLastPaid(0),
        nTimeLastWatchdogVote(mnb.sigTime),
        nActiveState(mnb.nActiveState),
        nCacheCollateralBlock(0),
        nBlockLastPaid(0),
        nProtocolVersion(mnb.nProtocolVersion),
        nPoSeBanScore(0),
        nPoSeBanHeight(0),
        fAllowMixingTx(true),
        fUnitTest(false) {}

//CSporkManager sporkManager;
//
// When a new bznode broadcast is sent, update our information
//
bool CBznode::UpdateFromNewBroadcast(CBznodeBroadcast &mnb) {
    if (mnb.sigTime <= sigTime && !mnb.fRecovery) return false;

    pubKeyBznode = mnb.pubKeyBznode;
    sigTime = mnb.sigTime;
    vchSig = mnb.vchSig;
    nProtocolVersion = mnb.nProtocolVersion;
    addr = mnb.addr;
    nPoSeBanScore = 0;
    nPoSeBanHeight = 0;
    nTimeLastChecked = 0;
    int nDos = 0;
    if (mnb.lastPing == CBznodePing() || (mnb.lastPing != CBznodePing() && mnb.lastPing.CheckAndUpdate(this, true, nDos))) {
        lastPing = mnb.lastPing;
        mnodeman.mapSeenBznodePing.insert(std::make_pair(lastPing.GetHash(), lastPing));
    }
    // if it matches our Bznode privkey...
    if (fBZNode && pubKeyBznode == activeBznode.pubKeyBznode) {
        nPoSeBanScore = -BZNODE_POSE_BAN_MAX_SCORE;
        if (nProtocolVersion == PROTOCOL_VERSION) {
            // ... and PROTOCOL_VERSION, then we've been remotely activated ...
            activeBznode.ManageState();
        } else {
            // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
            // but also do not ban the node we get this message from
            LogPrintf("CBznode::UpdateFromNewBroadcast -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", nProtocolVersion, PROTOCOL_VERSION);
            return false;
        }
    }
    return true;
}

//
// Deterministically calculate a given "score" for a Bznode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
arith_uint256 CBznode::CalculateScore(const uint256 &blockHash) {
    uint256 aux = ArithToUint256(UintToArith256(vin.prevout.hash) + vin.prevout.n);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << blockHash;
    arith_uint256 hash2 = UintToArith256(ss.GetHash());

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << blockHash;
    ss2 << aux;
    arith_uint256 hash3 = UintToArith256(ss2.GetHash());

    return (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);
}

void CBznode::Check(bool fForce) {
    LOCK(cs);

    if (ShutdownRequested()) return;

    if (!fForce && (GetTime() - nTimeLastChecked < BZNODE_CHECK_SECONDS)) return;
    nTimeLastChecked = GetTime();

    LogPrint("bznode", "CBznode::Check -- Bznode %s is in %s state\n", vin.prevout.ToStringShort(), GetStateString());

    //once spent, stop doing the checks
    if (IsOutpointSpent()) return;

    int nHeight = 0;
    if (!fUnitTest) {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) return;

        CCoins coins;
        if (!pcoinsTip->GetCoins(vin.prevout.hash, coins) ||
            (unsigned int) vin.prevout.n >= coins.vout.size() ||
            coins.vout[vin.prevout.n].IsNull()) {
            nActiveState = BZNODE_OUTPOINT_SPENT;
            LogPrint("bznode", "CBznode::Check -- Failed to find Bznode UTXO, bznode=%s\n", vin.prevout.ToStringShort());
            return;
        }

        nHeight = chainActive.Height();
    }

    if (IsPoSeBanned()) {
        if (nHeight < nPoSeBanHeight) return; // too early?
        // Otherwise give it a chance to proceed further to do all the usual checks and to change its state.
        // Bznode still will be on the edge and can be banned back easily if it keeps ignoring mnverify
        // or connect attempts. Will require few mnverify messages to strengthen its position in mn list.
        LogPrintf("CBznode::Check -- Bznode %s is unbanned and back in list now\n", vin.prevout.ToStringShort());
        DecreasePoSeBanScore();
    } else if (nPoSeBanScore >= BZNODE_POSE_BAN_MAX_SCORE) {
        nActiveState = BZNODE_POSE_BAN;
        // ban for the whole payment cycle
        nPoSeBanHeight = nHeight + mnodeman.size();
        LogPrintf("CBznode::Check -- Bznode %s is banned till block %d now\n", vin.prevout.ToStringShort(), nPoSeBanHeight);
        return;
    }

    int nActiveStatePrev = nActiveState;
    bool fOurBznode = fBZNode && activeBznode.pubKeyBznode == pubKeyBznode;

    // bznode doesn't meet payment protocol requirements ...
/*    bool fRequireUpdate = nProtocolVersion < mnpayments.GetMinBznodePaymentsProto() ||
                          // or it's our own node and we just updated it to the new protocol but we are still waiting for activation ...
                          (fOurBznode && nProtocolVersion < PROTOCOL_VERSION); */

    // bznode doesn't meet payment protocol requirements ...
    bool fRequireUpdate = nProtocolVersion < mnpayments.GetMinBznodePaymentsProto();

    if (fRequireUpdate) {
        nActiveState = BZNODE_UPDATE_REQUIRED;
        if (nActiveStatePrev != nActiveState) {
            LogPrint("bznode", "CBznode::Check -- Bznode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
        }
        return;
    }

    // keep old bznodes on start, give them a chance to receive updates...
    bool fWaitForPing = !bznodeSync.IsBznodeListSynced() && !IsPingedWithin(BZNODE_MIN_MNP_SECONDS);

    if (fWaitForPing && !fOurBznode) {
        // ...but if it was already expired before the initial check - return right away
        if (IsExpired() || IsWatchdogExpired() || IsNewStartRequired()) {
            LogPrint("bznode", "CBznode::Check -- Bznode %s is in %s state, waiting for ping\n", vin.prevout.ToStringShort(), GetStateString());
            return;
        }
    }

    // don't expire if we are still in "waiting for ping" mode unless it's our own bznode
    if (!fWaitForPing || fOurBznode) {

        if (!IsPingedWithin(BZNODE_NEW_START_REQUIRED_SECONDS)) {
            nActiveState = BZNODE_NEW_START_REQUIRED;
            if (nActiveStatePrev != nActiveState) {
                LogPrint("bznode", "CBznode::Check -- Bznode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }

        bool fWatchdogActive = bznodeSync.IsSynced() && mnodeman.IsWatchdogActive();
        bool fWatchdogExpired = (fWatchdogActive && ((GetTime() - nTimeLastWatchdogVote) > BZNODE_WATCHDOG_MAX_SECONDS));

//        LogPrint("bznode", "CBznode::Check -- outpoint=%s, nTimeLastWatchdogVote=%d, GetTime()=%d, fWatchdogExpired=%d\n",
//                vin.prevout.ToStringShort(), nTimeLastWatchdogVote, GetTime(), fWatchdogExpired);

        if (fWatchdogExpired) {
            nActiveState = BZNODE_WATCHDOG_EXPIRED;
            if (nActiveStatePrev != nActiveState) {
                LogPrint("bznode", "CBznode::Check -- Bznode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }

        if (!IsPingedWithin(BZNODE_EXPIRATION_SECONDS)) {
            nActiveState = BZNODE_EXPIRED;
            if (nActiveStatePrev != nActiveState) {
                LogPrint("bznode", "CBznode::Check -- Bznode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }
    }

    if (lastPing.sigTime - sigTime < BZNODE_MIN_MNP_SECONDS) {
        nActiveState = BZNODE_PRE_ENABLED;
        if (nActiveStatePrev != nActiveState) {
            LogPrint("bznode", "CBznode::Check -- Bznode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
        }
        return;
    }

    nActiveState = BZNODE_ENABLED; // OK
    if (nActiveStatePrev != nActiveState) {
        LogPrint("bznode", "CBznode::Check -- Bznode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
    }
}

bool CBznode::IsValidNetAddr() {
    return IsValidNetAddr(addr);
}

bool CBznode::IsValidForPayment() {

    if (nActiveState == BZNODE_ENABLED) {
        return true;
    }

    return false;
}

bool CBznode::IsValidNetAddr(CService addrIn) {
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkIDString() == CBaseChainParams::REGTEST ||
           (addrIn.IsIPv4() && IsReachable(addrIn) && addrIn.IsRoutable());
}

bznode_info_t CBznode::GetInfo() {
    bznode_info_t info;
    info.vin = vin;
    info.addr = addr;
    info.pubKeyCollateralAddress = pubKeyCollateralAddress;
    info.pubKeyBznode = pubKeyBznode;
    info.sigTime = sigTime;
    info.nLastDsq = nLastDsq;
    info.nTimeLastChecked = nTimeLastChecked;
    info.nTimeLastPaid = nTimeLastPaid;
    info.nTimeLastWatchdogVote = nTimeLastWatchdogVote;
    info.nTimeLastPing = lastPing.sigTime;
    info.nActiveState = nActiveState;
    info.nProtocolVersion = nProtocolVersion;
    info.fInfoValid = true;
    return info;
}

std::string CBznode::StateToString(int nStateIn) {
    switch (nStateIn) {
        case BZNODE_PRE_ENABLED:
            return "PRE_ENABLED";
        case BZNODE_ENABLED:
            return "ENABLED";
        case BZNODE_EXPIRED:
            return "EXPIRED";
        case BZNODE_OUTPOINT_SPENT:
            return "OUTPOINT_SPENT";
        case BZNODE_UPDATE_REQUIRED:
            return "UPDATE_REQUIRED";
        case BZNODE_WATCHDOG_EXPIRED:
            return "WATCHDOG_EXPIRED";
        case BZNODE_NEW_START_REQUIRED:
            return "NEW_START_REQUIRED";
        case BZNODE_POSE_BAN:
            return "POSE_BAN";
        default:
            return "UNKNOWN";
    }
}

std::string CBznode::GetStateString() const {
    return StateToString(nActiveState);
}

std::string CBznode::GetStatus() const {
    // TODO: return smth a bit more human readable here
    return GetStateString();
}

std::string CBznode::ToString() const {
    std::string str;
    str += "bznode{";
    str += addr.ToString();
    str += " ";
    str += std::to_string(nProtocolVersion);
    str += " ";
    str += vin.prevout.ToStringShort();
    str += " ";
    str += CBitcoinAddress(pubKeyCollateralAddress.GetID()).ToString();
    str += " ";
    str += std::to_string(lastPing == CBznodePing() ? sigTime : lastPing.sigTime);
    str += " ";
    str += std::to_string(lastPing == CBznodePing() ? 0 : lastPing.sigTime - sigTime);
    str += " ";
    str += std::to_string(nBlockLastPaid);
    str += "}\n";
    return str;
}

int CBznode::GetCollateralAge() {
    int nHeight;
    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain || !chainActive.Tip()) return -1;
        nHeight = chainActive.Height();
    }

    if (nCacheCollateralBlock == 0) {
        int nInputAge = GetInputAge(vin);
        if (nInputAge > 0) {
            nCacheCollateralBlock = nHeight - nInputAge;
        } else {
            return nInputAge;
        }
    }

    return nHeight - nCacheCollateralBlock;
}

void CBznode::UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack) {
    if (!pindex) {
        LogPrintf("CBznode::UpdateLastPaid pindex is NULL\n");
        return;
    }

    const Consensus::Params &params = Params().GetConsensus();
    const CBlockIndex *BlockReading = pindex;

    CScript mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());
    LogPrint("bznode", "CBznode::UpdateLastPaidBlock -- searching for block with payment to %s\n", vin.prevout.ToStringShort());

    LOCK(cs_mapBznodeBlocks);

    for (int i = 0; BlockReading && BlockReading->nHeight > nBlockLastPaid && i < nMaxBlocksToScanBack; i++) {
//        LogPrintf("mnpayments.mapBznodeBlocks.count(BlockReading->nHeight)=%s\n", mnpayments.mapBznodeBlocks.count(BlockReading->nHeight));
//        LogPrintf("mnpayments.mapBznodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)=%s\n", mnpayments.mapBznodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2));
        if (mnpayments.mapBznodeBlocks.count(BlockReading->nHeight) &&
            mnpayments.mapBznodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)) {
            // LogPrintf("i=%s, BlockReading->nHeight=%s\n", i, BlockReading->nHeight);
            CBlock block;
            if (!ReadBlockFromDisk(block, BlockReading, Params().GetConsensus())) // shouldn't really happen
            {
                LogPrintf("ReadBlockFromDisk failed\n");
                continue;
            }
            CAmount nBznodePayment = GetBznodePayment(BlockReading->nHeight);

            BOOST_FOREACH(CTxOut txout, block.vtx[0].vout)
            if (mnpayee == txout.scriptPubKey && nBznodePayment == txout.nValue) {
                nBlockLastPaid = BlockReading->nHeight;
                nTimeLastPaid = BlockReading->nTime;
                LogPrint("bznode", "CBznode::UpdateLastPaidBlock -- searching for block with payment to %s -- found new %d\n", vin.prevout.ToStringShort(), nBlockLastPaid);
                return;
            }
        }

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    // Last payment for this bznode wasn't found in latest mnpayments blocks
    // or it was found in mnpayments blocks but wasn't found in the blockchain.
    // LogPrint("bznode", "CBznode::UpdateLastPaidBlock -- searching for block with payment to %s -- keeping old %d\n", vin.prevout.ToStringShort(), nBlockLastPaid);
}

bool CBznodeBroadcast::Create(std::string strService, std::string strKeyBznode, std::string strTxHash, std::string strOutputIndex, std::string &strErrorRet, CBznodeBroadcast &mnbRet, bool fOffline) {
    LogPrintf("CBznodeBroadcast::Create\n");
    CTxIn txin;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyBznodeNew;
    CKey keyBznodeNew;
    //need correct blocks to send ping
    if (!fOffline && !bznodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Bznode";
        LogPrintf("CBznodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    //TODO
    if (!darkSendSigner.GetKeysFromSecret(strKeyBznode, keyBznodeNew, pubKeyBznodeNew)) {
        strErrorRet = strprintf("Invalid bznode key %s", strKeyBznode);
        LogPrintf("CBznodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!pwalletMain->GetBznodeVinAndKeys(txin, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex)) {
        strErrorRet = strprintf("Could not allocate txin %s:%s for bznode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CBznodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    CService service = CService(strService);
    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort) {
            strErrorRet = strprintf("Invalid port %u for bznode %s, only %d is supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
            LogPrintf("CBznodeBroadcast::Create -- %s\n", strErrorRet);
            return false;
        }
    } else if (service.GetPort() == mainnetDefaultPort) {
        strErrorRet = strprintf("Invalid port %u for bznode %s, %d is the only supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
        LogPrintf("CBznodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    return Create(txin, CService(strService), keyCollateralAddressNew, pubKeyCollateralAddressNew, keyBznodeNew, pubKeyBznodeNew, strErrorRet, mnbRet);
}

bool CBznodeBroadcast::Create(CTxIn txin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyBznodeNew, CPubKey pubKeyBznodeNew, std::string &strErrorRet, CBznodeBroadcast &mnbRet) {
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("bznode", "CBznodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyBznodeNew.GetID() = %s\n",
             CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
             pubKeyBznodeNew.GetID().ToString());


    CBznodePing mnp(txin);
    if (!mnp.Sign(keyBznodeNew, pubKeyBznodeNew)) {
        strErrorRet = strprintf("Failed to sign ping, bznode=%s", txin.prevout.ToStringShort());
        LogPrintf("CBznodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CBznodeBroadcast();
        return false;
    }

    mnbRet = CBznodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyBznodeNew, PROTOCOL_VERSION);

    if (!mnbRet.IsValidNetAddr()) {
        strErrorRet = strprintf("Invalid IP address, bznode=%s", txin.prevout.ToStringShort());
        LogPrintf("CBznodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CBznodeBroadcast();
        return false;
    }

    mnbRet.lastPing = mnp;
    if (!mnbRet.Sign(keyCollateralAddressNew)) {
        strErrorRet = strprintf("Failed to sign broadcast, bznode=%s", txin.prevout.ToStringShort());
        LogPrintf("CBznodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CBznodeBroadcast();
        return false;
    }

    return true;
}

bool CBznodeBroadcast::SimpleCheck(int &nDos) {
    nDos = 0;

    // make sure addr is valid
    if (!IsValidNetAddr()) {
        LogPrintf("CBznodeBroadcast::SimpleCheck -- Invalid addr, rejected: bznode=%s  addr=%s\n",
                  vin.prevout.ToStringShort(), addr.ToString());
        return false;
    }

    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CBznodeBroadcast::SimpleCheck -- Signature rejected, too far into the future: bznode=%s\n", vin.prevout.ToStringShort());
        nDos = 1;
        return false;
    }

    // empty ping or incorrect sigTime/unknown blockhash
    if (lastPing == CBznodePing() || !lastPing.SimpleCheck(nDos)) {
        // one of us is probably forked or smth, just mark it as expired and check the rest of the rules
        nActiveState = BZNODE_EXPIRED;
    }

    if (nProtocolVersion < mnpayments.GetMinBznodePaymentsProto()) {
        LogPrintf("CBznodeBroadcast::SimpleCheck -- ignoring outdated Bznode: bznode=%s  nProtocolVersion=%d\n", vin.prevout.ToStringShort(), nProtocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if (pubkeyScript.size() != 25) {
        LogPrintf("CBznodeBroadcast::SimpleCheck -- pubKeyCollateralAddress has the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyBznode.GetID());

    if (pubkeyScript2.size() != 25) {
        LogPrintf("CBznodeBroadcast::SimpleCheck -- pubKeyBznode has the wrong size\n");
        nDos = 100;
        return false;
    }

    if (!vin.scriptSig.empty()) {
        LogPrintf("CBznodeBroadcast::SimpleCheck -- Ignore Not Empty ScriptSig %s\n", vin.ToString());
        nDos = 100;
        return false;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (addr.GetPort() != mainnetDefaultPort) return false;
    } else if (addr.GetPort() == mainnetDefaultPort) return false;

    return true;
}

bool CBznodeBroadcast::Update(CBznode *pmn, int &nDos) {
    nDos = 0;

    if (pmn->sigTime == sigTime && !fRecovery) {
        // mapSeenBznodeBroadcast in CBznodeMan::CheckMnbAndUpdateBznodeList should filter legit duplicates
        // but this still can happen if we just started, which is ok, just do nothing here.
        return false;
    }

    // this broadcast is older than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    if (pmn->sigTime > sigTime) {
        LogPrintf("CBznodeBroadcast::Update -- Bad sigTime %d (existing broadcast is at %d) for Bznode %s %s\n",
                  sigTime, pmn->sigTime, vin.prevout.ToStringShort(), addr.ToString());
        return false;
    }

    pmn->Check();

    // bznode is banned by PoSe
    if (pmn->IsPoSeBanned()) {
        LogPrintf("CBznodeBroadcast::Update -- Banned by PoSe, bznode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    // IsVnAssociatedWithPubkey is validated once in CheckOutpoint, after that they just need to match
    if (pmn->pubKeyCollateralAddress != pubKeyCollateralAddress) {
        LogPrintf("CBznodeBroadcast::Update -- Got mismatched pubKeyCollateralAddress and vin\n");
        nDos = 33;
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CBznodeBroadcast::Update -- CheckSignature() failed, bznode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    // if ther was no bznode broadcast recently or if it matches our Bznode privkey...
    if (!pmn->IsBroadcastedWithin(BZNODE_MIN_MNB_SECONDS) || (fBZNode && pubKeyBznode == activeBznode.pubKeyBznode)) {
        // take the newest entry
        LogPrintf("CBznodeBroadcast::Update -- Got UPDATED Bznode entry: addr=%s\n", addr.ToString());
        if (pmn->UpdateFromNewBroadcast((*this))) {
            pmn->Check();
            RelayBZNode();
        }
        bznodeSync.AddedBznodeList();
    }

    return true;
}

bool CBznodeBroadcast::CheckOutpoint(int &nDos) {
    // we are a bznode with the same vin (i.e. already activated) and this mnb is ours (matches our Bznode privkey)
    // so nothing to do here for us
    if (fBZNode && vin.prevout == activeBznode.vin.prevout && pubKeyBznode == activeBznode.pubKeyBznode) {
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CBznodeBroadcast::CheckOutpoint -- CheckSignature() failed, bznode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) {
            // not mnb fault, let it to be checked again later
            LogPrint("bznode", "CBznodeBroadcast::CheckOutpoint -- Failed to aquire lock, addr=%s", addr.ToString());
            mnodeman.mapSeenBznodeBroadcast.erase(GetHash());
            return false;
        }

        CCoins coins;
        if (!pcoinsTip->GetCoins(vin.prevout.hash, coins) ||
            (unsigned int) vin.prevout.n >= coins.vout.size() ||
            coins.vout[vin.prevout.n].IsNull()) {
            LogPrint("bznode", "CBznodeBroadcast::CheckOutpoint -- Failed to find Bznode UTXO, bznode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
        if (coins.vout[vin.prevout.n].nValue != BZNODE_COIN_REQUIRED * COIN) {
            LogPrint("bznode", "CBznodeBroadcast::CheckOutpoint -- Bznode UTXO should have 1000 BZX, bznode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
        if (chainActive.Height() - coins.nHeight + 1 < Params().GetConsensus().nBznodeMinimumConfirmations) {
            LogPrintf("CBznodeBroadcast::CheckOutpoint -- Bznode UTXO must have at least %d confirmations, bznode=%s\n",
                      Params().GetConsensus().nBznodeMinimumConfirmations, vin.prevout.ToStringShort());
            // maybe we miss few blocks, let this mnb to be checked again later
            mnodeman.mapSeenBznodeBroadcast.erase(GetHash());
            return false;
        }
    }

    LogPrint("bznode", "CBznodeBroadcast::CheckOutpoint -- Bznode UTXO verified\n");

    // make sure the vout that was signed is related to the transaction that spawned the Bznode
    //  - this is expensive, so it's only done once per Bznode
    if (!darkSendSigner.IsVinAssociatedWithPubkey(vin, pubKeyCollateralAddress)) {
        LogPrintf("CBznodeMan::CheckOutpoint -- Got mismatched pubKeyCollateralAddress and vin\n");
        nDos = 33;
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 1000 BZX tx got nBznodeMinimumConfirmations
    uint256 hashBlock = uint256();
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, Params().GetConsensus(), hashBlock, true);
    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex *pMNIndex = (*mi).second; // block for 1000 BZX tx -> 1 confirmation
            CBlockIndex *pConfIndex = chainActive[pMNIndex->nHeight + Params().GetConsensus().nBznodeMinimumConfirmations - 1]; // block where tx got nBznodeMinimumConfirmations
            if (pConfIndex->GetBlockTime() > sigTime) {
                LogPrintf("CBznodeBroadcast::CheckOutpoint -- Bad sigTime %d (%d conf block is at %d) for Bznode %s %s\n",
                          sigTime, Params().GetConsensus().nBznodeMinimumConfirmations, pConfIndex->GetBlockTime(), vin.prevout.ToStringShort(), addr.ToString());
                return false;
            }
        }
    }

    return true;
}

bool CBznodeBroadcast::Sign(CKey &keyCollateralAddress) {
    std::string strError;
    std::string strMessage;

    sigTime = GetAdjustedTime();

    strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) +
                 pubKeyCollateralAddress.GetID().ToString() + pubKeyBznode.GetID().ToString() +
                 boost::lexical_cast<std::string>(nProtocolVersion);

    if (!darkSendSigner.SignMessage(strMessage, vchSig, keyCollateralAddress)) {
        LogPrintf("CBznodeBroadcast::Sign -- SignMessage() failed\n");
        return false;
    }

    if (!darkSendSigner.VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)) {
        LogPrintf("CBznodeBroadcast::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CBznodeBroadcast::CheckSignature(int &nDos) {
    std::string strMessage;
    std::string strError = "";
    nDos = 0;

    strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) +
                 pubKeyCollateralAddress.GetID().ToString() + pubKeyBznode.GetID().ToString() +
                 boost::lexical_cast<std::string>(nProtocolVersion);

    LogPrint("bznode", "CBznodeBroadcast::CheckSignature -- strMessage: %s  pubKeyCollateralAddress address: %s  sig: %s\n", strMessage, CBitcoinAddress(pubKeyCollateralAddress.GetID()).ToString(), EncodeBase64(&vchSig[0], vchSig.size()));

    if (!darkSendSigner.VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)) {
        LogPrintf("CBznodeBroadcast::CheckSignature -- Got bad Bznode announce signature, error: %s\n", strError);
        nDos = 100;
        return false;
    }

    return true;
}

void CBznodeBroadcast::RelayBZNode() {
    LogPrintf("CBznodeBroadcast::RelayBZNode\n");
    CInv inv(MSG_BZNODE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

CBznodePing::CBznodePing(CTxIn &vinNew) {
    LOCK(cs_main);
    if (!chainActive.Tip() || chainActive.Height() < 12) return;

    vin = vinNew;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector < unsigned char > ();
}

bool CBznodePing::Sign(CKey &keyBznode, CPubKey &pubKeyBznode) {
    std::string strError;
    std::string strBZNodeSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if (!darkSendSigner.SignMessage(strMessage, vchSig, keyBznode)) {
        LogPrintf("CBznodePing::Sign -- SignMessage() failed\n");
        return false;
    }

    if (!darkSendSigner.VerifyMessage(pubKeyBznode, vchSig, strMessage, strError)) {
        LogPrintf("CBznodePing::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CBznodePing::CheckSignature(CPubKey &pubKeyBznode, int &nDos) {
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
    std::string strError = "";
    nDos = 0;

    if (!darkSendSigner.VerifyMessage(pubKeyBznode, vchSig, strMessage, strError)) {
        LogPrintf("CBznodePing::CheckSignature -- Got bad Bznode ping signature, bznode=%s, error: %s\n", vin.prevout.ToStringShort(), strError);
        nDos = 33;
        return false;
    }
    return true;
}

bool CBznodePing::SimpleCheck(int &nDos) {
    // don't ban by default
    nDos = 0;

    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CBznodePing::SimpleCheck -- Signature rejected, too far into the future, bznode=%s\n", vin.prevout.ToStringShort());
        nDos = 1;
        return false;
    }

    {
//        LOCK(cs_main);
        AssertLockHeld(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if (mi == mapBlockIndex.end()) {
            LogPrint("bznode", "CBznodePing::SimpleCheck -- Bznode ping is invalid, unknown block hash: bznode=%s blockHash=%s\n", vin.prevout.ToStringShort(), blockHash.ToString());
            // maybe we stuck or forked so we shouldn't ban this node, just fail to accept this ping
            // TODO: or should we also request this block?
            return false;
        }
    }
    LogPrint("bznode", "CBznodePing::SimpleCheck -- Bznode ping verified: bznode=%s  blockHash=%s  sigTime=%d\n", vin.prevout.ToStringShort(), blockHash.ToString(), sigTime);
    return true;
}

bool CBznodePing::CheckAndUpdate(CBznode *pmn, bool fFromNewBroadcast, int &nDos) {
    // don't ban by default
    nDos = 0;

    if (!SimpleCheck(nDos)) {
        return false;
    }

    if (pmn == NULL) {
        LogPrint("bznode", "CBznodePing::CheckAndUpdate -- Couldn't find Bznode entry, bznode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    if (!fFromNewBroadcast) {
        if (pmn->IsUpdateRequired()) {
            LogPrint("bznode", "CBznodePing::CheckAndUpdate -- bznode protocol is outdated, bznode=%s\n", vin.prevout.ToStringShort());
            return false;
        }

        if (pmn->IsNewStartRequired()) {
            LogPrint("bznode", "CBznodePing::CheckAndUpdate -- bznode is completely expired, new start is required, bznode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
    }

    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if ((*mi).second && (*mi).second->nHeight < chainActive.Height() - 24) {
            // LogPrintf("CBznodePing::CheckAndUpdate -- Bznode ping is invalid, block hash is too old: bznode=%s  blockHash=%s\n", vin.prevout.ToStringShort(), blockHash.ToString());
            // nDos = 1;
            return false;
        }
    }

    LogPrint("bznode", "CBznodePing::CheckAndUpdate -- New ping: bznode=%s  blockHash=%s  sigTime=%d\n", vin.prevout.ToStringShort(), blockHash.ToString(), sigTime);

    // LogPrintf("mnping - Found corresponding mn for vin: %s\n", vin.prevout.ToStringShort());
    // update only if there is no known ping for this bznode or
    // last ping was more then BZNODE_MIN_MNP_SECONDS-60 ago comparing to this one
    if (pmn->IsPingedWithin(BZNODE_MIN_MNP_SECONDS - 60, sigTime)) {
        LogPrint("bznode", "CBznodePing::CheckAndUpdate -- Bznode ping arrived too early, bznode=%s\n", vin.prevout.ToStringShort());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }

    if (!CheckSignature(pmn->pubKeyBznode, nDos)) return false;

    // so, ping seems to be ok

    // if we are still syncing and there was no known ping for this mn for quite a while
    // (NOTE: assuming that BZNODE_EXPIRATION_SECONDS/2 should be enough to finish mn list sync)
    if (!bznodeSync.IsBznodeListSynced() && !pmn->IsPingedWithin(BZNODE_EXPIRATION_SECONDS / 2)) {
        // let's bump sync timeout
        LogPrint("bznode", "CBznodePing::CheckAndUpdate -- bumping sync timeout, bznode=%s\n", vin.prevout.ToStringShort());
        bznodeSync.AddedBznodeList();
    }

    // let's store this ping as the last one
    LogPrint("bznode", "CBznodePing::CheckAndUpdate -- Bznode ping accepted, bznode=%s\n", vin.prevout.ToStringShort());
    pmn->lastPing = *this;

    // and update mnodeman.mapSeenBznodeBroadcast.lastPing which is probably outdated
    CBznodeBroadcast mnb(*pmn);
    uint256 hash = mnb.GetHash();
    if (mnodeman.mapSeenBznodeBroadcast.count(hash)) {
        mnodeman.mapSeenBznodeBroadcast[hash].second.lastPing = *this;
    }

    pmn->Check(true); // force update, ignoring cache
    if (!pmn->IsEnabled()) return false;

    LogPrint("bznode", "CBznodePing::CheckAndUpdate -- Bznode ping acceepted and relayed, bznode=%s\n", vin.prevout.ToStringShort());
    Relay();

    return true;
}

void CBznodePing::Relay() {
    CInv inv(MSG_BZNODE_PING, GetHash());
    RelayInv(inv);
}

//void CBznode::AddGovernanceVote(uint256 nGovernanceObjectHash)
//{
//    if(mapGovernanceObjectsVotedOn.count(nGovernanceObjectHash)) {
//        mapGovernanceObjectsVotedOn[nGovernanceObjectHash]++;
//    } else {
//        mapGovernanceObjectsVotedOn.insert(std::make_pair(nGovernanceObjectHash, 1));
//    }
//}

//void CBznode::RemoveGovernanceObject(uint256 nGovernanceObjectHash)
//{
//    std::map<uint256, int>::iterator it = mapGovernanceObjectsVotedOn.find(nGovernanceObjectHash);
//    if(it == mapGovernanceObjectsVotedOn.end()) {
//        return;
//    }
//    mapGovernanceObjectsVotedOn.erase(it);
//}

void CBznode::UpdateWatchdogVoteTime() {
    LOCK(cs);
    nTimeLastWatchdogVote = GetTime();
}

/**
*   FLAG GOVERNANCE ITEMS AS DIRTY
*
*   - When bznode come and go on the network, we must flag the items they voted on to recalc it's cached flags
*
*/
//void CBznode::FlagGovernanceItemsAsDirty()
//{
//    std::vector<uint256> vecDirty;
//    {
//        std::map<uint256, int>::iterator it = mapGovernanceObjectsVotedOn.begin();
//        while(it != mapGovernanceObjectsVotedOn.end()) {
//            vecDirty.push_back(it->first);
//            ++it;
//        }
//    }
//    for(size_t i = 0; i < vecDirty.size(); ++i) {
//        mnodeman.AddDirtyGovernanceObjectHash(vecDirty[i]);
//    }
//}
