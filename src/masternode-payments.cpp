// Copyright (c) 2014-2019 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "consensus/validation.h"
#include "base58.h"
#include "script/standard.h"
#include "init.h"
#include "masternode-payments.h"
#include "masternode-sync.h"
#include "messagesigner.h"
#include "netfulfilledman.h"
#include "netmessagemaker.h"
#include "util.h"
#include "validation.h"

#include "evo/deterministicmns.h"

#include <string>

CMasternodePayments mnpayments;

bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string& strErrorRet)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    bool isBlockRewardValueMet = (block.vtx[0]->GetValueOut() <= blockReward);
   
    return isBlockRewardValueMet;
}

bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward)
{
    if(fLiteMode) {
        //there is no budget data to use to check anything, let's just accept the longest chain
        if(fDebug) LogPrintf("%s -- WARNING: Not enough data, skipping block payee checks\n", __func__);
        return true;
    }

    // Check for correct masternode payment
    if (mnpayments.IsTransactionValid(txNew, nBlockHeight, blockReward)) {
        LogPrint("mnpayments", "%s -- Valid masternode payment at height %d: %s", __func__, nBlockHeight, txNew.ToString());
        return true;
    }

    LogPrintf("%s -- ERROR: Invalid masternode payment detected at height %d: %s", __func__, nBlockHeight, txNew.ToString());
    return false;
}

void FillBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, std::vector<CTxOut>& voutMasternodePaymentsRet)
{
    if (!mnpayments.GetMasternodeTxOuts(nBlockHeight, blockReward, voutMasternodePaymentsRet)) {
        LogPrint("mnpayments", "%s -- no masternode to pay (MN list probably empty)\n", __func__);
    }

    txNew.vout.insert(txNew.vout.end(), voutMasternodePaymentsRet.begin(), voutMasternodePaymentsRet.end());

    std::string voutMasternodeStr;
    for (const auto& txout : voutMasternodePaymentsRet) {
        // subtract MN payment from miner reward
        txNew.vout[0].nValue -= txout.nValue;
        if (!voutMasternodeStr.empty())
            voutMasternodeStr += ",";
        voutMasternodeStr += txout.ToString();
    }

    LogPrint("mnpayments", "%s -- nBlockHeight %d blockReward %lld voutMasternodePaymentsRet \"%s\" txNew %s", __func__,
                            nBlockHeight, blockReward, voutMasternodeStr, txNew.ToString());
}

std::string GetRequiredPaymentsString(int nBlockHeight, const CDeterministicMNCPtr &payee)
{
    std::string strPayee = "Unknown";
    if (payee) {
        CTxDestination dest;
        if (!ExtractDestination(payee->pdmnState->scriptPayout, dest))
            assert(false);
        strPayee = CBitcoinAddress(dest).ToString();
    }

    return strPayee;
}

std::map<int, std::string> GetRequiredPaymentsStrings(int nStartHeight, int nEndHeight)
{
    std::map<int, std::string> mapPayments;

    if (nStartHeight < 1) {
        nStartHeight = 1;
    }

    LOCK(cs_main);
    int nChainTipHeight = chainActive.Height();

    bool doProjection = false;
    for(int h = nStartHeight; h < nEndHeight; h++) {
        if (h <= nChainTipHeight) {
            auto payee = deterministicMNManager->GetListForBlock(chainActive[h - 1]).GetMNPayee();
            mapPayments.emplace(h, GetRequiredPaymentsString(h, payee));
        } else {
            doProjection = true;
            break;
        }
    }
    if (doProjection) {
        auto projection = deterministicMNManager->GetListAtChainTip().GetProjectedMNPayees(nEndHeight - nChainTipHeight);
        for (size_t i = 0; i < projection.size(); i++) {
            auto payee = projection[i];
            int h = nChainTipHeight + 1 + i;
            mapPayments.emplace(h, GetRequiredPaymentsString(h, payee));
        }
    }

    return mapPayments;
}

/**
*   GetMasternodeTxOuts
*
*   Get masternode payment tx outputs
*/

bool CMasternodePayments::GetMasternodeTxOuts(int nBlockHeight, CAmount blockReward, std::vector<CTxOut>& voutMasternodePaymentsRet) const
{
    // make sure it's not filled yet
    voutMasternodePaymentsRet.clear();

    if(!GetBlockTxOuts(nBlockHeight, blockReward, voutMasternodePaymentsRet) && deterministicMNManager->IsDIP3Enforced(nBlockHeight))
    {
        LogPrintf("CMasternodePayments::%s -- no payee (deterministic masternode list empty)\n", __func__);
        return false;
    }

    for (const auto& txout : voutMasternodePaymentsRet) {
        CTxDestination address1;
        ExtractDestination(txout.scriptPubKey, address1);
        CBitcoinAddress address2(address1);

        LogPrintf("CMasternodePayments::%s -- Masternode payment %lld to %s\n", __func__, txout.nValue, address2.ToString());
    }

    return true;
}

bool CMasternodePayments::GetBlockTxOuts(int nBlockHeight, CAmount blockReward, std::vector<CTxOut>& voutMasternodePaymentsRet) const
{
    voutMasternodePaymentsRet.clear();
    if (nBlockHeight == 0) {
        return false;
    }

    CAmount masternodeReward = GetMasternodePayment(nBlockHeight);

    const CBlockIndex* pindex;
    {
        LOCK(cs_main);
        pindex = chainActive[nBlockHeight - 1];
    }
    uint256 proTxHash;
    auto dmnPayee = deterministicMNManager->GetListForBlock(pindex).GetMNPayee();
    if (!dmnPayee) {
        return false;
    }

    CAmount operatorReward = 0;
    if (dmnPayee->nOperatorReward != 0 && dmnPayee->pdmnState->scriptOperatorPayout != CScript()) {
        // This calculation might eventually turn out to result in 0 even if an operator reward percentage is given.
        // This will however only happen in a few years when the block rewards drops very low.
        operatorReward = (masternodeReward * dmnPayee->nOperatorReward) / 10000;
        masternodeReward -= operatorReward;
    }

    if (masternodeReward > 0) {
        voutMasternodePaymentsRet.emplace_back(masternodeReward, dmnPayee->pdmnState->scriptPayout);
    }
    if (operatorReward > 0) {
        voutMasternodePaymentsRet.emplace_back(operatorReward, dmnPayee->pdmnState->scriptOperatorPayout);
    }

    return true;
}

// Is this masternode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 blocks of votes
bool CMasternodePayments::IsScheduled(const CDeterministicMNCPtr& dmnIn, int nNotBlockHeight) const
{
    auto projectedPayees = deterministicMNManager->GetListAtChainTip().GetProjectedMNPayees(8);
    for (const auto &dmn : projectedPayees) {
        if (dmn->proTxHash == dmnIn->proTxHash) {
            return true;
        }
    }
    return false;
}

bool CMasternodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward) const
{
    std::vector<CTxOut> voutMasternodePayments;
    if (!GetBlockTxOuts(nBlockHeight, blockReward, voutMasternodePayments)) {
        LogPrintf("CMasternodePayments::%s -- ERROR failed to get payees for block at height %s\n", __func__, nBlockHeight);
        return false;
    }

    for (const auto& txout : voutMasternodePayments) {
        bool found = false;
        for (const auto& txout2 : txNew.vout) {
            if (txout == txout2) {
                found = true;
                break;
            }
        }
        if (!found) {
            CTxDestination dest;
            if (!ExtractDestination(txout.scriptPubKey, dest))
                assert(false);
            LogPrintf("CMasternodePayments::%s -- ERROR failed to find expected payee %s in block at height %s\n", __func__, CBitcoinAddress(dest).ToString(), nBlockHeight);
            return false;
        }
    }
    return true;
}
