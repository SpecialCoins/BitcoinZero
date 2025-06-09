// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CHECKPOINTS_H
#define BITCOIN_CHECKPOINTS_H

#include "uint256.h"

#include <map>

class CBlockIndex;
struct CheckpointData;

/**
 * Block-chain checkpoints are compiled-in sanity checks.
 * They are updated every release or three.
 */
namespace Checkpoints
{
double GuessVerificationProgress(const CBlockIndex* pindex);
CBlockIndex* GetLastCheckpoint(const CheckpointData& data);
bool CheckBlock(const CheckpointData& data, int nHeight, const uint256& hash, bool fMatchesCheckpoint = false);
extern bool fEnabled;

} //namespace Checkpoints

#endif // BITCOIN_CHECKPOINTS_H
