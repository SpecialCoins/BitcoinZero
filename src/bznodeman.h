// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BZNODEMAN_H
#define BZNODEMAN_H

#include "bznode.h"
#include "sync.h"

using namespace std;

class CBznodeMan;

extern CBznodeMan mnodeman;

/**
 * Provides a forward and reverse index between MN vin's and integers.
 *
 * This mapping is normally add-only and is expected to be permanent
 * It is only rebuilt if the size of the index exceeds the expected maximum number
 * of MN's and the current number of known MN's.
 *
 * The external interface to this index is provided via delegation by CBznodeMan
 */
class CBznodeIndex
{
public: // Types
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

    typedef std::map<int,CTxIn> rindex_m_t;

    typedef rindex_m_t::iterator rindex_m_it;

    typedef rindex_m_t::const_iterator rindex_m_cit;

private:
    int                  nSize;

    index_m_t            mapIndex;

    rindex_m_t           mapReverseIndex;

public:
    CBznodeIndex();

    int GetSize() const {
        return nSize;
    }

    /// Retrieve bznode vin by index
    bool Get(int nIndex, CTxIn& vinBznode) const;

    /// Get index of a bznode vin
    int GetBznodeIndex(const CTxIn& vinBznode) const;

    void AddBznodeVIN(const CTxIn& vinBznode);

    void Clear();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapIndex);
        if(ser_action.ForRead()) {
            RebuildIndex();
        }
    }

private:
    void RebuildIndex();

};

class CBznodeMan
{
public:
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

private:
    static const int MAX_EXPECTED_INDEX_SIZE = 30000;

    /// Only allow 1 index rebuild per hour
    static const int64_t MIN_INDEX_REBUILD_TIME = 3600;

    static const std::string SERIALIZATION_VERSION_STRING;

    static const int DSEG_UPDATE_SECONDS        = 3 * 60 * 60;

    static const int LAST_PAID_SCAN_BLOCKS      = 100;

    static const int MIN_POSE_PROTO_VERSION     = 70203;
    static const int MAX_POSE_CONNECTIONS       = 10;
    static const int MAX_POSE_RANK              = 10;
    static const int MAX_POSE_BLOCKS            = 10;

    static const int MNB_RECOVERY_QUORUM_TOTAL      = 10;
    static const int MNB_RECOVERY_QUORUM_REQUIRED   = 6;
    static const int MNB_RECOVERY_MAX_ASK_ENTRIES   = 10;
    static const int MNB_RECOVERY_WAIT_SECONDS      = 60;
    static const int MNB_RECOVERY_RETRY_SECONDS     = 3 * 60 * 60;


    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

    // map to hold all MNs
    std::vector<CBznode> vBznodes;
    // who's asked for the Bznode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForBznodeList;
    // who we asked for the Bznode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForBznodeList;
    // which Bznodes we've asked for
    std::map<COutPoint, std::map<CNetAddr, int64_t> > mWeAskedForBznodeListEntry;
    // who we asked for the bznode verification
    std::map<CNetAddr, CBznodeVerification> mWeAskedForVerification;

    // these maps are used for bznode recovery from BZNODE_NEW_START_REQUIRED state
    std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > > mMnbRecoveryRequests;
    std::map<uint256, std::vector<CBznodeBroadcast> > mMnbRecoveryGoodReplies;
    std::list< std::pair<CService, uint256> > listScheduledMnbRequestConnections;

    int64_t nLastIndexRebuildTime;

    CBznodeIndex indexBznodes;

    CBznodeIndex indexBznodesOld;

    /// Set when index has been rebuilt, clear when read
    bool fIndexRebuilt;

    /// Set when bznodes are added, cleared when CGovernanceManager is notified
    bool fBznodesAdded;

    /// Set when bznodes are removed, cleared when CGovernanceManager is notified
    bool fBznodesRemoved;

    std::vector<uint256> vecDirtyGovernanceObjectHashes;

    int64_t nLastWatchdogVoteTime;

    friend class CBznodeSync;

public:
    // Keep track of all broadcasts I've seen
    std::map<uint256, std::pair<int64_t, CBznodeBroadcast> > mapSeenBznodeBroadcast;
    // Keep track of all pings I've seen
    std::map<uint256, CBznodePing> mapSeenBznodePing;
    // Keep track of all verifications I've seen
    std::map<uint256, CBznodeVerification> mapSeenBznodeVerification;
    // keep track of dsq count to prevent bznodes from gaming darksend queue
    int64_t nDsqCount;


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        std::string strVersion;
        if(ser_action.ForRead()) {
            READWRITE(strVersion);
        }
        else {
            strVersion = SERIALIZATION_VERSION_STRING; 
            READWRITE(strVersion);
        }

        READWRITE(vBznodes);
        READWRITE(mAskedUsForBznodeList);
        READWRITE(mWeAskedForBznodeList);
        READWRITE(mWeAskedForBznodeListEntry);
        READWRITE(mMnbRecoveryRequests);
        READWRITE(mMnbRecoveryGoodReplies);
        READWRITE(nLastWatchdogVoteTime);
        READWRITE(nDsqCount);

        READWRITE(mapSeenBznodeBroadcast);
        READWRITE(mapSeenBznodePing);
        READWRITE(indexBznodes);
        if(ser_action.ForRead() && (strVersion != SERIALIZATION_VERSION_STRING)) {
            Clear();
        }
    }

    CBznodeMan();

    /// Add an entry
    bool Add(CBznode &mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode *pnode, const CTxIn &vin);
    void AskForMnb(CNode *pnode, const uint256 &hash);

    /// Check all Bznodes
    void Check();

    /// Check all Bznodes and remove inactive
    void CheckAndRemove();

    /// Clear Bznode vector
    void Clear();

    /// Count Bznodes filtered by nProtocolVersion.
    /// Bznode nProtocolVersion should match or be above the one specified in param here.
    int CountBznodes(int nProtocolVersion = -1);
    /// Count enabled Bznodes filtered by nProtocolVersion.
    /// Bznode nProtocolVersion should match or be above the one specified in param here.
    int CountEnabled(int nProtocolVersion = -1);

    /// Count Bznodes by network type - NET_IPV4, NET_IPV6, NET_TOR
    // int CountByIP(int nNetworkType);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CBznode* Find(const CScript &payee);
    CBznode* Find(const CTxIn& vin);
    CBznode* Find(const CPubKey& pubKeyBznode);

    /// Versions of Find that are safe to use from outside the class
    bool Get(const CPubKey& pubKeyBznode, CBznode& bznode);
    bool Get(const CTxIn& vin, CBznode& bznode);

    /// Retrieve bznode vin by index
    bool Get(int nIndex, CTxIn& vinBznode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexBznodes.Get(nIndex, vinBznode);
    }

    bool GetIndexRebuiltFlag() {
        LOCK(cs);
        return fIndexRebuilt;
    }

    /// Get index of a bznode vin
    int GetBznodeIndex(const CTxIn& vinBznode) {
        LOCK(cs);
        return indexBznodes.GetBznodeIndex(vinBznode);
    }

    /// Get old index of a bznode vin
    int GetBznodeIndexOld(const CTxIn& vinBznode) {
        LOCK(cs);
        return indexBznodesOld.GetBznodeIndex(vinBznode);
    }

    /// Get bznode VIN for an old index value
    bool GetBznodeVinForIndexOld(int nBznodeIndex, CTxIn& vinBznodeOut) {
        LOCK(cs);
        return indexBznodesOld.Get(nBznodeIndex, vinBznodeOut);
    }

    /// Get index of a bznode vin, returning rebuild flag
    int GetBznodeIndex(const CTxIn& vinBznode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexBznodes.GetBznodeIndex(vinBznode);
    }

    void ClearOldBznodeIndex() {
        LOCK(cs);
        indexBznodesOld.Clear();
        fIndexRebuilt = false;
    }

    bool Has(const CTxIn& vin);

    bznode_info_t GetBznodeInfo(const CTxIn& vin);

    bznode_info_t GetBznodeInfo(const CPubKey& pubKeyBznode);

    char* GetNotQualifyReason(CBznode& mn, int nBlockHeight, bool fFilterSigTime, int nMnCount);

    /// Find an entry in the bznode list that is next to be paid
    CBznode* GetNextBznodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);
    /// Same as above but use current block height
    CBznode* GetNextBznodeInQueueForPayment(bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CBznode* FindRandomNotInVec(const std::vector<CTxIn> &vecToExclude, int nProtocolVersion = -1);

    std::vector<CBznode> GetFullBznodeVector() { LOCK(cs); return vBznodes; }

    std::vector<std::pair<int, CBznode> > GetBznodeRanks(int nBlockHeight = -1, int nMinProtocol=0);
    int GetBznodeRank(const CTxIn &vin, int nBlockHeight, int nMinProtocol=0, bool fOnlyActive=true);
    CBznode* GetBznodeByRank(int nRank, int nBlockHeight, int nMinProtocol=0, bool fOnlyActive=true);

    void ProcessBznodeConnections();
    std::pair<CService, std::set<uint256> > PopScheduledMnbRequestConnection();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    void DoFullVerificationStep();
    void CheckSameAddr();
    bool SendVerifyRequest(const CAddress& addr, const std::vector<CBznode*>& vSortedByAddr);
    void SendVerifyReply(CNode* pnode, CBznodeVerification& mnv);
    void ProcessVerifyReply(CNode* pnode, CBznodeVerification& mnv);
    void ProcessVerifyBroadcast(CNode* pnode, const CBznodeVerification& mnv);

    /// Return the number of (unique) Bznodes
    int size() { return vBznodes.size(); }

    std::string ToString() const;

    /// Update bznode list and maps using provided CBznodeBroadcast
    void UpdateBznodeList(CBznodeBroadcast mnb);
    /// Perform complete check and only then update list and maps
    bool CheckMnbAndUpdateBznodeList(CNode* pfrom, CBznodeBroadcast mnb, int& nDos);
    bool IsMnbRecoveryRequested(const uint256& hash) { return mMnbRecoveryRequests.count(hash); }

    void UpdateLastPaid();

    void CheckAndRebuildBznodeIndex();

    void AddDirtyGovernanceObjectHash(const uint256& nHash)
    {
        LOCK(cs);
        vecDirtyGovernanceObjectHashes.push_back(nHash);
    }

    std::vector<uint256> GetAndClearDirtyGovernanceObjectHashes()
    {
        LOCK(cs);
        std::vector<uint256> vecTmp = vecDirtyGovernanceObjectHashes;
        vecDirtyGovernanceObjectHashes.clear();
        return vecTmp;;
    }

    bool IsWatchdogActive();
    void UpdateWatchdogVoteTime(const CTxIn& vin);
    bool AddGovernanceVote(const CTxIn& vin, uint256 nGovernanceObjectHash);
    void RemoveGovernanceObject(uint256 nGovernanceObjectHash);

    void CheckBznode(const CTxIn& vin, bool fForce = false);
    void CheckBznode(const CPubKey& pubKeyBznode, bool fForce = false);

    int GetBznodeState(const CTxIn& vin);
    int GetBznodeState(const CPubKey& pubKeyBznode);

    bool IsBznodePingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt = -1);
    void SetBznodeLastPing(const CTxIn& vin, const CBznodePing& mnp);

    void UpdatedBlockTip(const CBlockIndex *pindex);

    /**
     * Called to notify CGovernanceManager that the bznode index has been updated.
     * Must be called while not holding the CBznodeMan::cs mutex
     */
    void NotifyBznodeUpdates();

};

#endif
