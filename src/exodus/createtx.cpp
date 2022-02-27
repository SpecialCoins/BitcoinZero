#include "createtx.h"

#include "packetencoder.h"
#include "script.h"

#include "../base58.h"
#include "../coins.h"
#include "../pubkey.h"
#include "../uint256.h"

#include "../primitives/transaction.h"

#include "../script/script.h"
#include "../script/standard.h"

#include <string>
#include <utility>
#include <vector>

#include <inttypes.h>

/** Creates a new previous output entry. */
PrevTxsEntry::PrevTxsEntry(const uint256& txid, uint32_t nOut, int64_t nValue, const CScript& scriptPubKey)
  : outPoint(txid, nOut), txOut(nValue, scriptPubKey)
{
}

/** Creates a new transaction builder. */
TxBuilder::TxBuilder()
{
}

/** Creates a new transaction builder to extend a transaction. */
TxBuilder::TxBuilder(const CMutableTransaction& transactionIn)
  : transaction(transactionIn)
{
}

/** Adds an outpoint as input to the transaction. */
TxBuilder& TxBuilder::addInput(const COutPoint& outPoint)
{
    CTxIn txIn(outPoint);
    transaction.vin.push_back(txIn);

    return *this;
}

/** Adds a transaction input to the transaction. */
TxBuilder& TxBuilder::addInput(const uint256& txid, uint32_t nOut)
{
    COutPoint outPoint(txid, nOut);

    return addInput(outPoint);
}

/** Adds an output to the transaction. */
TxBuilder& TxBuilder::addOutput(const CScript& scriptPubKey, int64_t value)
{
    CTxOut txOutput(value, scriptPubKey);
    transaction.vout.push_back(txOutput);

    return *this;
}

/** Adds a collection of outputs to the transaction. */
TxBuilder& TxBuilder::addOutputs(const std::vector<std::pair<CScript, int64_t> >& txOutputs)
{
    for (std::vector<std::pair<CScript, int64_t> >::const_iterator it = txOutputs.begin();
            it != txOutputs.end(); ++it) {
        addOutput(it->first, it->second);
    }

    return *this;
}

/** Adds an output for change. */
TxBuilder& TxBuilder::addChange(const CTxDestination& destination, const CCoinsViewCache& view, int64_t txFee, uint32_t position)
{
    CTransaction tx(transaction);

    if (!view.HaveInputs(tx)) {
        return *this;
    }

    CScript scriptPubKey = GetScriptForDestination(destination);

    int64_t txChange = view.GetValueIn(tx) - tx.GetValueOut() - txFee;
    int64_t minValue = GetDustThreshold(scriptPubKey);

    if (txChange < minValue) {
        return *this;
    }

    std::vector<CTxOut>::iterator it = transaction.vout.end();
    if (position < transaction.vout.size()) {
        it = transaction.vout.begin() + position;
    }

    CTxOut txOutput(txChange, scriptPubKey);
    transaction.vout.insert(it, txOutput);

    return *this;
}

/** Returns the created transaction. */
CMutableTransaction TxBuilder::build()
{
    return transaction;
}

/** Creates a new Exodus transaction builder. */
ExodusTxBuilder::ExodusTxBuilder()
  : TxBuilder()
{
}

/** Creates a new Exodus transaction builder to extend a transaction. */
ExodusTxBuilder::ExodusTxBuilder(const CMutableTransaction& transactionIn)
  : TxBuilder(transactionIn)
{
}

/** Adds a collection of previous outputs as inputs to the transaction. */
ExodusTxBuilder& ExodusTxBuilder::addInputs(const std::vector<PrevTxsEntry>& prevTxs)
{
    for (std::vector<PrevTxsEntry>::const_iterator it = prevTxs.begin();
            it != prevTxs.end(); ++it) {
        addInput(it->outPoint);
    }

    return *this;
}

/** Adds an output for the reference address. */
ExodusTxBuilder& ExodusTxBuilder::addReference(const std::string& destination, int64_t value)
{
    CBitcoinAddress addr(destination);
    CScript scriptPubKey = GetScriptForDestination(addr.Get());

    int64_t minValue = GetDustThreshold(scriptPubKey);
    value = std::max(minValue, value);

    return (ExodusTxBuilder&) TxBuilder::addOutput(scriptPubKey, value);
}

/** Embeds a payload with class C (op-return) encoding. */
ExodusTxBuilder& ExodusTxBuilder::addOpReturn(const std::vector<unsigned char>& data)
{
    transaction.vout.push_back(exodus::EncodeClassC(data.begin(), data.end()));
    return *this;
}

/** Embeds a payload with class B (bare-multisig) encoding. */
ExodusTxBuilder& ExodusTxBuilder::addMultisig(const std::vector<unsigned char>& data, const std::string& seed, const CPubKey& pubKey)
{
    exodus::EncodeClassB(seed, pubKey, data.begin(), data.end(), std::back_inserter(transaction.vout));
    return *this;
}

/** Adds an output for change. */
ExodusTxBuilder& ExodusTxBuilder::addChange(const std::string& destination, const CCoinsViewCache& view, int64_t txFee, uint32_t position)
{
    CBitcoinAddress addr(destination);

    return (ExodusTxBuilder&) TxBuilder::addChange(addr.Get(), view, txFee, position);
}

/** Adds previous transaction outputs to coins view. */
void InputsToView(const std::vector<PrevTxsEntry>& prevTxs, CCoinsViewCache& view)
{
    for (std::vector<PrevTxsEntry>::const_iterator it = prevTxs.begin(); it != prevTxs.end(); ++it) {
        CCoinsModifier coins = view.ModifyCoins(it->outPoint.hash);
        if ((size_t) it->outPoint.n >= coins->vout.size()) {
            coins->vout.resize(it->outPoint.n+1);
        }
        coins->vout[it->outPoint.n].scriptPubKey = it->txOut.scriptPubKey;
        coins->vout[it->outPoint.n].nValue = it->txOut.nValue;
    }
}
