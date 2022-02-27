// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activebznode.h"
#include "consensus/consensus.h"
#include "bznode.h"
#include "bznode-sync.h"
#include "bznode-payments.h"
#include "bznodeman.h"
#include "protocol.h"

extern CWallet *pwalletMain;

// Keep track of the active Bznode
CActiveBznode activeBznode;

void CActiveBznode::ManageState() {
    LogPrint("bznode", "CActiveBznode::ManageState -- Start\n");
    if (!fBZNode) {
        LogPrint("bznode", "CActiveBznode::ManageState -- Not a bznode, returning\n");
        return;
    }

    if (Params().NetworkIDString() != CBaseChainParams::REGTEST && !bznodeSync.IsBlockchainSynced()) {
        nState = ACTIVE_BZNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveBznode::ManageState -- %s: %s\n", GetStateString(), GetStatus());
        return;
    }

    if (nState == ACTIVE_BZNODE_SYNC_IN_PROCESS) {
        nState = ACTIVE_BZNODE_INITIAL;
    }

    LogPrint("bznode", "CActiveBznode::ManageState -- status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);

    if (eType == BZNODE_UNKNOWN) {
        ManageStateInitial();
    }

    if (eType == BZNODE_REMOTE) {
        ManageStateRemote();
    } else if (eType == BZNODE_LOCAL) {
        // Try Remote Start first so the started local bznode can be restarted without recreate bznode broadcast.
        ManageStateRemote();
        if (nState != ACTIVE_BZNODE_STARTED)
            ManageStateLocal();
    }

    SendBznodePing();
}

std::string CActiveBznode::GetStateString() const {
    switch (nState) {
        case ACTIVE_BZNODE_INITIAL:
            return "INITIAL";
        case ACTIVE_BZNODE_SYNC_IN_PROCESS:
            return "SYNC_IN_PROCESS";
        case ACTIVE_BZNODE_INPUT_TOO_NEW:
            return "INPUT_TOO_NEW";
        case ACTIVE_BZNODE_NOT_CAPABLE:
            return "NOT_CAPABLE";
        case ACTIVE_BZNODE_STARTED:
            return "STARTED";
        default:
            return "UNKNOWN";
    }
}

std::string CActiveBznode::GetStatus() const {
    switch (nState) {
        case ACTIVE_BZNODE_INITIAL:
            return "Node just started, not yet activated";
        case ACTIVE_BZNODE_SYNC_IN_PROCESS:
            return "Sync in progress. Must wait until sync is complete to start Bznode";
        case ACTIVE_BZNODE_INPUT_TOO_NEW:
            return strprintf("Bznode input must have at least %d confirmations",
                             Params().GetConsensus().nBznodeMinimumConfirmations);
        case ACTIVE_BZNODE_NOT_CAPABLE:
            return "Not capable bznode: " + strNotCapableReason;
        case ACTIVE_BZNODE_STARTED:
            return "Bznode successfully started";
        default:
            return "Unknown";
    }
}

std::string CActiveBznode::GetTypeString() const {
    std::string strType;
    switch (eType) {
        case BZNODE_UNKNOWN:
            strType = "UNKNOWN";
            break;
        case BZNODE_REMOTE:
            strType = "REMOTE";
            break;
        case BZNODE_LOCAL:
            strType = "LOCAL";
            break;
        default:
            strType = "UNKNOWN";
            break;
    }
    return strType;
}

bool CActiveBznode::SendBznodePing() {
    if (!fPingerEnabled) {
        LogPrint("bznode",
                 "CActiveBznode::SendBznodePing -- %s: bznode ping service is disabled, skipping...\n",
                 GetStateString());
        return false;
    }

    if (!mnodeman.Has(vin)) {
        strNotCapableReason = "Bznode not in bznode list";
        nState = ACTIVE_BZNODE_NOT_CAPABLE;
        LogPrintf("CActiveBznode::SendBznodePing -- %s: %s\n", GetStateString(), strNotCapableReason);
        return false;
    }

    CBznodePing mnp(vin);
    if (!mnp.Sign(keyBznode, pubKeyBznode)) {
        LogPrintf("CActiveBznode::SendBznodePing -- ERROR: Couldn't sign Bznode Ping\n");
        return false;
    }

    // Update lastPing for our bznode in Bznode list
    if (mnodeman.IsBznodePingedWithin(vin, BZNODE_MIN_MNP_SECONDS, mnp.sigTime)) {
        LogPrintf("CActiveBznode::SendBznodePing -- Too early to send Bznode Ping\n");
        return false;
    }

    mnodeman.SetBznodeLastPing(vin, mnp);

    LogPrintf("CActiveBznode::SendBznodePing -- Relaying ping, collateral=%s\n", vin.ToString());
    mnp.Relay();

    return true;
}

void CActiveBznode::ManageStateInitial() {
    LogPrint("bznode", "CActiveBznode::ManageStateInitial -- status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        nState = ACTIVE_BZNODE_NOT_CAPABLE;
        strNotCapableReason = "Bznode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("CActiveBznode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    bool fFoundLocal = false;
    {
        LOCK(cs_vNodes);

        // First try to find whatever local address is specified by externalip option
        fFoundLocal = GetLocal(service) && CBznode::IsValidNetAddr(service);
        if (!fFoundLocal) {
            // nothing and no live connections, can't do anything for now
            if (vNodes.empty()) {
                nState = ACTIVE_BZNODE_NOT_CAPABLE;
                strNotCapableReason = "Can't detect valid external address. Will retry when there are some connections available.";
                LogPrintf("CActiveBznode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
                return;
            }
            // We have some peers, let's try to find our local address from one of them
            BOOST_FOREACH(CNode * pnode, vNodes)
            {
                if (pnode->fSuccessfullyConnected && pnode->addr.IsIPv4()) {
                    fFoundLocal = GetLocal(service, &pnode->addr) && CBznode::IsValidNetAddr(service);
                    if (fFoundLocal) break;
                }
            }
        }
    }

    if (!fFoundLocal) {
        nState = ACTIVE_BZNODE_NOT_CAPABLE;
        strNotCapableReason = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("CActiveBznode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort) {
            nState = ACTIVE_BZNODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Invalid port: %u - only %d is supported on mainnet.", service.GetPort(),
                                            mainnetDefaultPort);
            LogPrintf("CActiveBznode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    } else if (service.GetPort() == mainnetDefaultPort) {
        nState = ACTIVE_BZNODE_NOT_CAPABLE;
        strNotCapableReason = strprintf("Invalid port: %u - %d is only supported on mainnet.", service.GetPort(),
                                        mainnetDefaultPort);
        LogPrintf("CActiveBznode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    LogPrintf("CActiveBznode::ManageStateInitial -- Checking inbound connection to '%s'\n", service.ToString());
    //TODO
    if (!ConnectNode(CAddress(service, NODE_NETWORK), NULL, false, true)) {
        nState = ACTIVE_BZNODE_NOT_CAPABLE;
        strNotCapableReason = "Could not connect to " + service.ToString();
        LogPrintf("CActiveBznode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // Default to REMOTE
    eType = BZNODE_REMOTE;

    // Check if wallet funds are available
    if (!pwalletMain) {
        LogPrintf("CActiveBznode::ManageStateInitial -- %s: Wallet not available\n", GetStateString());
        return;
    }

    if (pwalletMain->IsLocked()) {
        LogPrintf("CActiveBznode::ManageStateInitial -- %s: Wallet is locked\n", GetStateString());
        return;
    }

    if (pwalletMain->GetBalance() < BZNODE_COIN_REQUIRED * COIN) {
        LogPrintf("CActiveBznode::ManageStateInitial -- %s: Wallet balance is < 1000 BZX\n", GetStateString());
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    // If collateral is found switch to LOCAL mode
    if (pwalletMain->GetBznodeVinAndKeys(vin, pubKeyCollateral, keyCollateral)) {
        eType = BZNODE_LOCAL;
    }

    LogPrint("bznode", "CActiveBznode::ManageStateInitial -- End status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);
}

void CActiveBznode::ManageStateRemote() {
    LogPrint("bznode",
             "CActiveBznode::ManageStateRemote -- Start status = %s, type = %s, pinger enabled = %d, pubKeyBznode.GetID() = %s\n",
             GetStatus(), fPingerEnabled, GetTypeString(), pubKeyBznode.GetID().ToString());

    mnodeman.CheckBznode(pubKeyBznode);
    bznode_info_t infoMn = mnodeman.GetBznodeInfo(pubKeyBznode);
    if (infoMn.fInfoValid) {
        if (infoMn.nProtocolVersion < mnpayments.GetMinBznodePaymentsProto()) {
            nState = ACTIVE_BZNODE_NOT_CAPABLE;
            strNotCapableReason = "Invalid protocol version";
            LogPrintf("CActiveBznode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (service != infoMn.addr) {
            nState = ACTIVE_BZNODE_NOT_CAPABLE;
            // LogPrintf("service: %s\n", service.ToString());
            // LogPrintf("infoMn.addr: %s\n", infoMn.addr.ToString());
            strNotCapableReason = "Broadcasted IP doesn't match our external address. Make sure you issued a new broadcast if IP of this bznode changed recently.";
            LogPrintf("CActiveBznode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (!CBznode::IsValidStateForAutoStart(infoMn.nActiveState)) {
            nState = ACTIVE_BZNODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Bznode in %s state", CBznode::StateToString(infoMn.nActiveState));
            LogPrintf("CActiveBznode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (nState != ACTIVE_BZNODE_STARTED) {
            LogPrintf("CActiveBznode::ManageStateRemote -- STARTED!\n");
            vin = infoMn.vin;
            service = infoMn.addr;
            fPingerEnabled = true;
            nState = ACTIVE_BZNODE_STARTED;
        }
    } else {
        nState = ACTIVE_BZNODE_NOT_CAPABLE;
        strNotCapableReason = "Bznode not in bznode list";
        LogPrintf("CActiveBznode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
    }
}

void CActiveBznode::ManageStateLocal() {
    LogPrint("bznode", "CActiveBznode::ManageStateLocal -- status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);
    if (nState == ACTIVE_BZNODE_STARTED) {
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    if (pwalletMain->GetBznodeVinAndKeys(vin, pubKeyCollateral, keyCollateral)) {
        int nInputAge = GetInputAge(vin);
        if (nInputAge < Params().GetConsensus().nBznodeMinimumConfirmations) {
            nState = ACTIVE_BZNODE_INPUT_TOO_NEW;
            strNotCapableReason = strprintf(_("%s - %d confirmations"), GetStatus(), nInputAge);
            LogPrintf("CActiveBznode::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        {
            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);
        }

        CBznodeBroadcast mnb;
        std::string strError;
        if (!CBznodeBroadcast::Create(vin, service, keyCollateral, pubKeyCollateral, keyBznode,
                                     pubKeyBznode, strError, mnb)) {
            nState = ACTIVE_BZNODE_NOT_CAPABLE;
            strNotCapableReason = "Error creating mastenode broadcast: " + strError;
            LogPrintf("CActiveBznode::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        fPingerEnabled = true;
        nState = ACTIVE_BZNODE_STARTED;

        //update to bznode list
        LogPrintf("CActiveBznode::ManageStateLocal -- Update Bznode List\n");
        mnodeman.UpdateBznodeList(mnb);
        mnodeman.NotifyBznodeUpdates();

        //send to all peers
        LogPrintf("CActiveBznode::ManageStateLocal -- Relay broadcast, vin=%s\n", vin.ToString());
        mnb.RelayBZNode();
    }
}
