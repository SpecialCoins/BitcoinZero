// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BZNODE_SYNC_H
#define BZNODE_SYNC_H

#include "chain.h"
#include "net.h"

#include <univalue.h>

class CBznodeSync;

static const int BZNODE_SYNC_FAILED          = -1;
static const int BZNODE_SYNC_INITIAL         = 0;
static const int BZNODE_SYNC_SPORKS          = 1;
static const int BZNODE_SYNC_LIST            = 2;
static const int BZNODE_SYNC_MNW             = 3;
static const int BZNODE_SYNC_FINISHED        = 999;

static const int BZNODE_SYNC_TICK_SECONDS    = 6;
static const int BZNODE_SYNC_TIMEOUT_SECONDS = 30; // our blocks are 2.5 minutes so 30 seconds should be fine

static const int BZNODE_SYNC_ENOUGH_PEERS    = 3;

extern CBznodeSync bznodeSync;

//
// CBznodeSync : Sync bznode assets in stages
//

class CBznodeSync
{
private:
    // Keep track of current asset
    int nRequestedBznodeAssets;
    // Count peers we've requested the asset from
    int nRequestedBznodeAttempt;

    // Time when current bznode asset sync started
    int64_t nTimeAssetSyncStarted;

    // Last time when we received some bznode asset ...
    int64_t nTimeLastBznodeList;
    int64_t nTimeLastPaymentVote;
    int64_t nTimeLastGovernanceItem;
    // ... or failed
    int64_t nTimeLastFailure;

    // How many times we failed
    int nCountFailures;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

    bool CheckNodeHeight(CNode* pnode, bool fDisconnectStuckNodes = false);
    void Fail();
    void ClearFulfilledRequests();

public:
    CBznodeSync() { Reset(); }

    void AddedBznodeList() { nTimeLastBznodeList = GetTime(); }
    void AddedPaymentVote() { nTimeLastPaymentVote = GetTime(); }
    void AddedGovernanceItem() { nTimeLastGovernanceItem = GetTime(); };

    void SendGovernanceSyncRequest(CNode* pnode);

    bool IsFailed() { return nRequestedBznodeAssets == BZNODE_SYNC_FAILED; }
    bool IsBlockchainSynced(bool fBlockAccepted = false);
    bool IsBznodeListSynced() { return nRequestedBznodeAssets > BZNODE_SYNC_LIST; }
    bool IsWinnersListSynced() { return nRequestedBznodeAssets > BZNODE_SYNC_MNW; }
    bool IsSynced() { return nRequestedBznodeAssets == BZNODE_SYNC_FINISHED; }

    int GetAssetID() { return nRequestedBznodeAssets; }
    int GetAttempt() { return nRequestedBznodeAttempt; }
    std::string GetAssetName();
    std::string GetSyncStatus();

    void Reset();
    void SwitchToNextAsset();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void ProcessTick();

    void UpdatedBlockTip(const CBlockIndex *pindex);
};

#endif
