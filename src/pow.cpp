// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"
#include "validation.h"
#include "arith_uint256.h"
#include "chain.h"
#include "primitives/block.h"
#include "consensus/consensus.h"
#include "uint256.h"
#include <iostream>
#include "util.h"
#include "chainparams.h"
#include "bitcoin_bignum/bignum.h"

unsigned int static DarkGravityWave(const CBlockIndex* pindexPrev, const Consensus::Params& params)
{

    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    int64_t nPastBlocks = 24;

    // at utxo blocks, just return powLimit
    if (pindexPrev->nHeight <= Params().GetConsensus().utxo)
    {
        return bnPowLimit.GetCompact();
    }

    const CBlockIndex *pindex = pindexPrev;
    arith_uint256 bnPastTargetAvg;

    for (unsigned int nCountBlocks = 1; nCountBlocks <= nPastBlocks; nCountBlocks++) {
        arith_uint256 bnTarget = arith_uint256().SetCompact(pindex->nBits);
        if (nCountBlocks == 1) {
            bnPastTargetAvg = bnTarget;
        } else {
            // NOTE: that's not an average really...
            bnPastTargetAvg = (bnPastTargetAvg * nCountBlocks + bnTarget) / (nCountBlocks + 1);
        }

        if(nCountBlocks != nPastBlocks) {
            assert(pindex->pprev); // should never fail
            pindex = pindex->pprev;
        }
    }

    arith_uint256 bnNew(bnPastTargetAvg);

    int64_t nActualTimespan = pindexPrev->GetBlockTime() - pindex->GetBlockTime();
    // NOTE: is this accurate? nActualTimespan counts it for (nPastBlocks - 1) blocks only...
    int64_t nTargetTimespan = nPastBlocks * params.nPowTargetSpacing;

    if (nActualTimespan < nTargetTimespan/1.5)
        nActualTimespan = nTargetTimespan/1.5;
    if (nActualTimespan > nTargetTimespan*1.5)
        nActualTimespan = nTargetTimespan*1.5;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexPrev, const CBlockHeader *pblock)
{
    const Consensus::Params& params = ::Params().GetConsensus();
    if (pindexPrev->nHeight < Params().GetConsensus().nxt)
    {
        return DarkGravityWave(pindexPrev, params);
    }

    else
    {
        return NexxtDG(pindexPrev, pblock, Params().GetConsensus());
    }
}

unsigned int NexxtDG(const CBlockIndex* pindexPrev, const CBlockHeader *pblock, const Consensus::Params & Params)
{
        const Consensus::Params& params = ::Params().GetConsensus();
        const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
        int64_t nPastBlocks = 24;

        // at utxo blocks, just return powLimit
        if (pindexPrev->nHeight <= params.utxo)
        {
            return bnPowLimit.GetCompact();
        }

        const CBlockIndex *pindex = pindexPrev;
        arith_uint256 bnPastTargetAvg;

        for (unsigned int nCountBlocks = 1; nCountBlocks <= nPastBlocks; nCountBlocks++) {
            arith_uint256 bnTarget = arith_uint256().SetCompact(pindex->nBits);
            if (nCountBlocks == 1) {
                bnPastTargetAvg = bnTarget;
            } else {
                // NOTE: that's not an average really...
                bnPastTargetAvg = (bnPastTargetAvg * nCountBlocks + bnTarget) / (nCountBlocks + 1);
            }

            if(nCountBlocks != nPastBlocks) {
                assert(pindex->pprev); // should never fail
                pindex = pindex->pprev;
            }
        }

        arith_uint256 bnNew(bnPastTargetAvg);
        int64_t nActualTimespan = pindexPrev->GetBlockTime() - pindex->GetBlockTime();
        int64_t nTargetTimespan = nPastBlocks * Nexxt(pindexPrev, pblock, Params);
        bnNew *= nActualTimespan;
        bnNew /= nTargetTimespan;

        if (bnNew > bnPowLimit) {
            bnNew = bnPowLimit;
        }

        return bnNew.GetCompact();
}

int64_t Nexxt(const CBlockIndex* pindexPrev, const CBlockHeader* pblock, const Consensus::Params &params)
{
        if      (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (60 * 60))) {
            return params.nPowTargetSpacing / 150 * 1; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (55 * 60))) {
            return params.nPowTargetSpacing / 150 * 2; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (50 * 60))) {
            return params.nPowTargetSpacing / 150 * 4; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (45 * 60))) {
            return params.nPowTargetSpacing / 150 * 6; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (40 * 60))) {
            return params.nPowTargetSpacing / 150 * 8; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (35 * 60))) {
            return params.nPowTargetSpacing / 150 * 12; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (30 * 60))) {
            return params.nPowTargetSpacing / 150 * 16; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (25 * 60))) {
            return params.nPowTargetSpacing / 150 * 27; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (20 * 60))) {
            return params.nPowTargetSpacing / 150 * 41; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (15 * 60))) {
            return params.nPowTargetSpacing / 150 * 62; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (10 * 60))) {
            return params.nPowTargetSpacing / 150 * 106; }

        else {
            return (params.nPowTargetSpacing); }
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params &params) {
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);
    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;
    return true;
}
