// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"
#include "main.h"
#include "arith_uint256.h"
#include "chain.h"
#include "primitives/block.h"
#include "consensus/consensus.h"
#include "uint256.h"
#include <iostream>
#include "util.h"
#include "chainparams.h"
#include "libzerocoin/bitcoin_bignum/bignum.h"
#include "fixed.h"

static CBigNum bnProofOfWorkLimit(~arith_uint256(0) >> 12);

unsigned int static DarkGravityWave3(const CBlockIndex* pindexLast, const CBlockHeader *pblock) {

    const CBlockIndex *BlockReading = pindexLast;
    const CBlockHeader *BlockCreating = pblock;
    BlockCreating = BlockCreating;
    int64_t nActualTimespan = 0;
    int64_t LastBlockTime = 0;
    int64_t PastBlocksMin = 24;
    int64_t PastBlocksMax = 24;
    int64_t CountBlocks = 0;
    CBigNum PastDifficultyAverage;
    CBigNum PastDifficultyAveragePrev;

    // loop over the past n blocks, where n == PastBlocksMax
    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) { break; }
        CountBlocks++;

        // Calculate average difficulty based on the blocks we iterate over in this for loop
        if(CountBlocks <= PastBlocksMin) {
            if (CountBlocks == 1) { PastDifficultyAverage.SetCompact(BlockReading->nBits); }
            else { PastDifficultyAverage = ((PastDifficultyAveragePrev * CountBlocks)+(CBigNum().SetCompact(BlockReading->nBits))) / (CountBlocks+1); }
            PastDifficultyAveragePrev = PastDifficultyAverage;
        }

        // If this is the second iteration (LastBlockTime was set)
        if(LastBlockTime > 0){
            // Calculate time difference between previous block and current block
            int64_t Diff = (LastBlockTime - BlockReading->GetBlockTime());
            // Increment the actual timespan
            nActualTimespan += Diff;
        }
        // Set LasBlockTime to the block time for the block in current iteration
        LastBlockTime = BlockReading->GetBlockTime();

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    // bnNew is the difficulty
    CBigNum bnNew(PastDifficultyAverage);

    // nTargetTimespan is the time that the CountBlocks should have taken to be generated.
    int64_t nTargetTimespan = CountBlocks * 150;

    // We don't want to increase/decrease diff too much.
    if (nActualTimespan < nTargetTimespan/1.5)
        nActualTimespan = nTargetTimespan/1.5;
    if (nActualTimespan > nTargetTimespan*1.5)
        nActualTimespan = nTargetTimespan*1.5;

    // Calculate the new difficulty based on actual and target timespan.
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    // If calculated difficulty is lower than the minimal diff, set the new difficulty to be the minimal diff.
    if (bnNew > bnProofOfWorkLimit){
        bnNew = bnProofOfWorkLimit;
    }

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequiredBTC(const CBlockIndex* pindexLast, const CBlockHeader *pblock)
{
    // Go back by worth of blocks
    int nHeightFirst = pindexLast->nHeight - (2);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime());
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime)
{
    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < 150/1.25)
        nActualTimespan = 150/1.25;
    if (nActualTimespan > 150*1.25)
        nActualTimespan = 150*1.25;

    // Retarget
    const arith_uint256 bnPowLimit(~arith_uint256(0) >> 12);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= 150;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock)
{
    if (pindexLast->nHeight > HF_FORK_DGW)
    {
        return DarkGravityWave3(pindexLast, pblock);
    }
    else if (pindexLast->nHeight > HF_FORK_END)
    {
        return GetNextWorkRequiredBTC(pindexLast, pblock);
    }
    else
    {
        return bnProofOfWorkLimit.GetCompact();
    }
}

unsigned int NexxtDG(const CBlockIndex* pindexLast, const CBlockHeader *pblock)
{
    const CBlockIndex *BlockReading = pindexLast;
    const CBlockHeader *BlockCreating = pblock;
    BlockCreating = BlockCreating;
    int64_t nActualTimespan = 0;
    int64_t LastBlockTime = 0;
    int64_t PastBlocksMin = 24;
    int64_t PastBlocksMax = 24;
    int64_t CountBlocks = 0;
    CBigNum PastDifficultyAverage;
    CBigNum PastDifficultyAveragePrev;

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) { break; }
        CountBlocks++;

        if(CountBlocks <= PastBlocksMin) {
            if (CountBlocks == 1) { PastDifficultyAverage.SetCompact(BlockReading->nBits); }
            else { PastDifficultyAverage = ((PastDifficultyAveragePrev * CountBlocks)+(CBigNum().SetCompact(BlockReading->nBits))) / (CountBlocks+1); }
            PastDifficultyAveragePrev = PastDifficultyAverage;
        }

        if(LastBlockTime > 0){
            int64_t Diff = (LastBlockTime - BlockReading->GetBlockTime());
            nActualTimespan += Diff;
        }
        LastBlockTime = BlockReading->GetBlockTime();

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = BlockReading->pprev;
    }

    CBigNum bnNew(PastDifficultyAverage);
    int64_t nTargetTimespan = CountBlocks * Nexxt(pindexLast, pblock);
    bnNew *= nActualTimespan;
    bnNew /= nTargetTimespan;

    if (bnNew > bnProofOfWorkLimit){
        bnNew = bnProofOfWorkLimit;
    }

    return bnNew.GetCompact();
}

unsigned int NexxtD(const CBlockIndex* pindexPrev, const CBlockHeader* pblock)
{
    int nHeight = pindexPrev->nHeight + 1;
    if (nHeight > 52033)
    {
        return NexxtDG(pindexPrev, pblock);
    }
    else
    {
        return GetNextWorkRequired(pindexPrev, pblock);
    }
}

int64_t Nexxt(const CBlockIndex* pindexPrev, const CBlockHeader* pblock)//add on the fly check
{
        if      (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (60 * 60) + 0)) {
            return 1; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (55 * 60) + 0)) {
            return 2; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (50 * 60) + 0)) {
            return 4; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (45 * 60) + 0)) {
            return 6; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (40 * 60) + 0)) {
            return 8; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (35 * 60) + 0)) {
            return 12; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (30 * 60) + 0)) {
            return 18; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (25 * 60) + 0)) {
            return 27; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (20 * 60) + 0)) {
            return 41; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (15 * 60) + 0)) {
            return 62; }

        else if (pblock->GetBlockTime() > (pindexPrev->GetBlockTime() + (10 * 60) + 0)) {
            return 106; }

        else {
            return 150; }
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
