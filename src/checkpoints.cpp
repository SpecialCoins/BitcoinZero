// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "checkpoints.h"

#include "chain.h"
#include "chainparams.h"
#include "validation.h"
#include "uint256.h"
#include <reverse_iterate.h>

#include <stdint.h>

#include <boost/foreach.hpp>

static const double SIGCHECK_VERIFICATION_FACTOR = 5.0;

namespace Checkpoints {

bool fEnabled = true;

//! Guess how far we are in the verification process at the given block index
double GuessVerificationProgress(const CBlockIndex* pindex)
{
    if (pindex == NULL)
        return 0.0;

    int64_t nNow = time(NULL);
    double fWorkBefore = 0.0; // Amount of work done before pindex
    double fWorkAfter = 0.0;  // Amount of work left after pindex (estimated)
    const ChainTxData& data = Params().TxData();

    if (pindex->nChainTx <= data.nTransactionsLastCheckpoint) {
        double nCheapBefore = pindex->nChainTx;
        double nCheapAfter = data.nTransactionsLastCheckpoint - pindex->nChainTx;
        double nExpensiveAfter = (nNow - data.nTimeLastCheckpoint) / 86400.0 * data.fTransactionsPerDay;
        fWorkBefore = nCheapBefore;
        fWorkAfter = nCheapAfter + nExpensiveAfter;
    } else {
        double nCheapBefore = data.nTransactionsLastCheckpoint;
        double nExpensiveBefore = pindex->nChainTx - data.nTransactionsLastCheckpoint;
        double nExpensiveAfter = (nNow - pindex->GetBlockTime()) / 86400.0 * data.fTransactionsPerDay;
        fWorkBefore = nCheapBefore + nExpensiveBefore;
        fWorkAfter = nExpensiveAfter;
    }

    return fWorkBefore / (fWorkBefore + fWorkAfter);
}

CBlockIndex* GetLastCheckpoint(const CheckpointData& data)
{
    const MapCheckpoints& checkpoints = data.mapCheckpoints;

    for (const MapCheckpoints::value_type& i : reverse_iterate(checkpoints))
    {
        const uint256& hash = i.second;
        CBlockIndex* pindex = LookupBlockIndex(hash);
        if (pindex) {
            return pindex;
        }
    }
    return nullptr;
}

bool CheckBlock(const CheckpointData& data, int nHeight, const uint256& hash, bool fMatchesCheckpoint)
{
    if (!fEnabled)
        return true;

    const MapCheckpoints& checkpoints = data.mapCheckpoints;

    MapCheckpoints::const_iterator i = checkpoints.find(nHeight);
    // If looking for an exact match, then return false
    if (i == checkpoints.end()) return !fMatchesCheckpoint;
    return hash == i->second;
}

} // namespace Checkpoints
