// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BZNODE_PAYMENTS_H
#define BZNODE_PAYMENTS_H

#include "util.h"
#include "core_io.h"
#include "key.h"
#include "main.h"
#include "bznode.h"
#include "utilstrencodings.h"

class CBznodePayments;
class CBznodePaymentVote;
class CBznodeBlockPayees;

static const int MNPAYMENTS_SIGNATURES_REQUIRED         = 6;
static const int MNPAYMENTS_SIGNATURES_TOTAL            = 10;

extern CCriticalSection cs_vecPayees;
extern CCriticalSection cs_mapBznodeBlocks;
extern CCriticalSection cs_mapBznodePayeeVotes;

extern CBznodePayments mnpayments;

/// TODO: all 4 functions do not belong here really, they should be refactored/moved somewhere (main.cpp ?)
bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward);
bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward);
void FillBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutBznodeRet);
std::string GetRequiredPaymentsString(int nBlockHeight);

class CBznodePayee
{
private:
    CScript scriptPubKey;
    std::vector<uint256> vecVoteHashes;

public:
    CBznodePayee() :
        scriptPubKey(),
        vecVoteHashes()
        {}

    CBznodePayee(CScript payee, uint256 hashIn) :
        scriptPubKey(payee),
        vecVoteHashes()
    {
        vecVoteHashes.push_back(hashIn);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(vecVoteHashes);
    }

    CScript GetPayee() { return scriptPubKey; }

    void AddVoteHash(uint256 hashIn) { vecVoteHashes.push_back(hashIn); }
    std::vector<uint256> GetVoteHashes() { return vecVoteHashes; }
    int GetVoteCount() { return vecVoteHashes.size(); }
    std::string ToString() const;
};

// Keep track of votes for payees from bznodes
class CBznodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CBznodePayee> vecPayees;

    CBznodeBlockPayees() :
        nBlockHeight(0),
        vecPayees()
        {}
    CBznodeBlockPayees(int nBlockHeightIn) :
        nBlockHeight(nBlockHeightIn),
        vecPayees()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nBlockHeight);
        READWRITE(vecPayees);
    }

    void AddPayee(const CBznodePaymentVote& vote);
    bool GetBestPayee(CScript& payeeRet);
    bool HasPayeeWithVotes(CScript payeeIn, int nVotesReq);

    bool IsTransactionValid(const CTransaction& txNew);

    std::string GetRequiredPaymentsString();
};

// vote for the winning payment
class CBznodePaymentVote
{
public:
    CTxIn vinBznode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CBznodePaymentVote() :
        vinBznode(),
        nBlockHeight(0),
        payee(),
        vchSig()
        {}

    CBznodePaymentVote(CTxIn vinBznode, int nBlockHeight, CScript payee) :
        vinBznode(vinBznode),
        nBlockHeight(nBlockHeight),
        payee(payee),
        vchSig()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vinBznode);
        READWRITE(nBlockHeight);
        READWRITE(*(CScriptBase*)(&payee));
        READWRITE(vchSig);
    }

    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << *(CScriptBase*)(&payee);
        ss << nBlockHeight;
        ss << vinBznode.prevout;
        return ss.GetHash();
    }

    bool Sign();
    bool CheckSignature(const CPubKey& pubKeyBznode, int nValidationHeight, int &nDos);

    bool IsValid(CNode* pnode, int nValidationHeight, std::string& strError);
    void Relay();

    bool IsVerified() { return !vchSig.empty(); }
    void MarkAsNotVerified() { vchSig.clear(); }

    std::string ToString() const;
};

//
// Bznode Payments Class
// Keeps track of who should get paid for which blocks
//

class CBznodePayments
{
private:
    // bznode count times nStorageCoeff payments blocks should be stored ...
    const float nStorageCoeff;
    // ... but at least nMinBlocksToStore (payments blocks)
    const int nMinBlocksToStore;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

public:
    std::map<uint256, CBznodePaymentVote> mapBznodePaymentVotes;
    std::map<int, CBznodeBlockPayees> mapBznodeBlocks;
    std::map<COutPoint, int> mapBznodesLastVote;

    CBznodePayments() : nStorageCoeff(1.25), nMinBlocksToStore(5000) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(mapBznodePaymentVotes);
        READWRITE(mapBznodeBlocks);
    }

    void Clear();

    bool AddPaymentVote(const CBznodePaymentVote& vote);
    bool HasVerifiedPaymentVote(uint256 hashIn);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node);
    void RequestLowDataPaymentBlocks(CNode* pnode);
    void CheckAndRemove();

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CBznode& mn, int nNotBlockHeight);

    bool CanVote(COutPoint outBznode, int nBlockHeight);

    int GetMinBznodePaymentsProto();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutBznodeRet);
    std::string ToString() const;

    int GetBlockCount() { return mapBznodeBlocks.size(); }
    int GetVoteCount() { return mapBznodePaymentVotes.size(); }

    bool IsEnoughData();
    int GetStorageLimit();

    void UpdatedBlockTip(const CBlockIndex *pindex);
};

#endif
