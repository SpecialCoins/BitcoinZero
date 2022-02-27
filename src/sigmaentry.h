// Copyright (c) 2019 The BitcoinZero Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SIGMAENTRY_H
#define SIGMAENTRY_H

#include <amount.h>
#include <streams.h>
#include <boost/optional.hpp>
#include <limits.h>
#include "libzerocoin/bitcoin_bignum/bignum.h"
#include "libzerocoin/Zerocoin.h"
#include "key.h"
#include "sigma/coin.h"
#include "serialize.h"
#include "sigma_params.h"


struct CMintMeta
{
    int nHeight;
    int nId;
    GroupElement const & GetPubCoinValue() const;
    void SetPubCoinValue(GroupElement const & other);
    uint256 GetPubCoinValueHash() const;
    uint256 hashSerial;
    uint8_t nVersion;
    sigma::CoinDenomination denom;
    uint256 txid;
    bool isUsed;
    bool isArchived;
    bool isDeterministic;
    bool isSeedCorrect;
private:
    GroupElement pubCoinValue;
    mutable boost::optional<uint256> pubCoinValueHash;
};

class CSigmaEntry
{
public:
    void set_denomination(sigma::CoinDenomination denom) {
        DenominationToInteger(denom, denomination);
    }
    void set_denomination_value(int64_t new_denomination) {
        denomination = new_denomination;
    }
    int64_t get_denomination_value() const {
        return denomination;
    }
    sigma::CoinDenomination get_denomination() const {
        sigma::CoinDenomination result;
        IntegerToDenomination(denomination, result);
        return result;
    }
    std::string get_string_denomination() const {
        return DenominationToString(get_denomination());
    }

    //public
    GroupElement value;

    //private
    Scalar randomness;
    Scalar serialNumber;

    // Signature over partial transaction
    // to make sure the outputs are not changed by attacker.
    std::vector<unsigned char> ecdsaSecretKey;

    bool IsUsed;
    int nHeight;
    int id;

private:
    // NOTE(martun): made this one private to make sure people don't
    // misuse it and try to assign a value of type sigma::CoinDenomination
    // to it. In these cases the value is automatically converted to int,
    // which is not what we want.
    // Starting from Version 3 == sigma, this number is coin value * COIN,
    // I.E. it is set to 100.000.000 for 1 coin.
    int64_t denomination;

public:

    CSigmaEntry()
    {
        SetNull();
    }

    void SetNull()
    {
        IsUsed = false;
        randomness = Scalar(uint64_t(0));
        serialNumber = Scalar(uint64_t(0));
        value = GroupElement();
        denomination = -1;
        nHeight = -1;
        id = -1;
    }

    bool IsCorrectSigmaMint() const {
        return randomness.isMember() && serialNumber.isMember();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(IsUsed);
        READWRITE(randomness);
        READWRITE(serialNumber);
        READWRITE(value);
        READWRITE(denomination);
        READWRITE(nHeight);
        READWRITE(id);
        if (ser_action.ForRead())
        {
            if (!is_eof(s))
            {
                    READWRITE(ecdsaSecretKey);

            }
        }
    }
private:
    template <typename Stream>
    auto is_eof_helper(Stream &s, bool) -> decltype(s.eof()) {
        return s.eof();
    }

    template <typename Stream>
    bool is_eof_helper(Stream &s, int) {
        return false;
    }

    template<typename Stream>
    bool is_eof(Stream &s) {
        return is_eof_helper(s, true);
    }
};

class CSigmaSpendEntry
{
public:
    Scalar coinSerial;
    uint256 hashTx;
    GroupElement pubCoin;
    int id;

    void set_denomination(sigma::CoinDenomination denom) {
        DenominationToInteger(denom, denomination);
    }

    void set_denomination_value(int64_t new_denomination) {
        denomination = new_denomination;
    }

    int64_t get_denomination_value() const {
        return denomination;
    }

    sigma::CoinDenomination get_denomination() const {
        sigma::CoinDenomination result;
        IntegerToDenomination(denomination, result);
        return result;
    }

    CSigmaSpendEntry()
    {
        SetNull();
    }

    void SetNull()
    {
        coinSerial = Scalar(uint64_t(0));
//        hashTx =
        pubCoin = GroupElement();
        denomination = 0;
        id = 0;
    }
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(coinSerial);
        READWRITE(hashTx);
        READWRITE(pubCoin);
        READWRITE(denomination);
        READWRITE(id);
    }
private:
    // NOTE(martun): made this one private to make sure people don't
    // misuse it and try to assign a value of type sigma::CoinDenomination
    // to it. In these cases the value is automatically converted to int,
    // which is not what we want.
    // Starting from Version 3 == sigma, this number is coin value * COIN,
    // I.E. it is set to 100.000.000 for 1 coin.
    int64_t denomination;
};

namespace primitives {
uint256 GetSerialHash(const secp_primitives::Scalar& bnSerial);
uint256 GetPubCoinValueHash(const secp_primitives::GroupElement& bnValue);
}

#endif //SIGMAENTRY_H
