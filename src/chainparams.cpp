// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/merkle.h"
#include "consensus/consensus.h"
#include "priv_params.h"

#include "tinyformat.h"
#include "util.h"
#include "utilstrencodings.h"
#include "bitcoin_bignum/bignum.h"
#include "blacklists.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>

#include "chainparamsseeds.h"
#include "arith_uint256.h"

using namespace secp_primitives;

static CBlock CreateGenesisBlock(const char* pszTimestamp, const CScript& genesisOutputScript, uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount& genesisReward)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
    txNew.vout[0].nValue = genesisReward;
    txNew.vout[0].scriptPubKey = genesisOutputScript;

    CBlock genesis;
    genesis.nTime    = nTime;
    genesis.nBits    = nBits;
    genesis.nNonce   = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion, const CAmount &genesisReward)
{
    const char *pszTimestamp = "BZX burn 2021";
    const CScript genesisOutputScript = CScript();
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, genesisReward);
}

static Consensus::LLMQParams llmq50_60 = {
        .type = Consensus::LLMQ_50_60,
        .name = "llmq_50_60",
        .size = 50,
        .minSize = 40,
        .threshold = 30,

        .dkgInterval = 18, // one DKG per 90 minutes
        .dkgPhaseBlocks = 2,
        .dkgMiningWindowStart = 10, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 16,
        .dkgBadVotesThreshold = 40,

        .signingActiveQuorumCount = 16, // a full day worth of LLMQs

        .keepOldConnections = 17,
};

static Consensus::LLMQParams llmq400_60 = {
        .type = Consensus::LLMQ_400_60,
        .name = "llmq_400_60",
        .size = 400,
        .minSize = 300,
        .threshold = 240,

        .dkgInterval = 12 * 12, // one DKG every 12 hours
        .dkgPhaseBlocks = 4,
        .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 28,
        .dkgBadVotesThreshold = 300,

        .signingActiveQuorumCount = 4, // two days worth of LLMQs

        .keepOldConnections = 5,
};

// Used for deployment and min-proto-version signalling, so it needs a higher threshold
static Consensus::LLMQParams llmq400_85 = {
        .type = Consensus::LLMQ_400_85,
        .name = "llmq_400_85",
        .size = 400,
        .minSize = 350,
        .threshold = 340,

        .dkgInterval = 12 * 24, // one DKG every 24 hours
        .dkgPhaseBlocks = 4,
        .dkgMiningWindowStart = 20, // dkgPhaseBlocks * 5 = after finalization
        .dkgMiningWindowEnd = 48, // give it a larger mining window to make sure it is mined
        .dkgBadVotesThreshold = 300,

        .signingActiveQuorumCount = 4, // two days worth of LLMQs

        .keepOldConnections = 5,
};


/**
 * Main network
**/

class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";

        consensus.chainType = Consensus::chainMain;

        consensus.nMajorityEnforceBlockUpgrade = 750;
        consensus.nMajorityRejectBlockOutdated = 950;
        consensus.nMajorityWindow = 1000;
        consensus.powLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.nPowTargetTimespan = 150; //
        consensus.nPowTargetSpacing = 150; //
        consensus.nRuleChangeActivationThreshold = 1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 2016; //
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1475020800; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00000000000000000000000000000000000000000000000000043d640ee04132");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0xd1e22bc4bdc42e4d9208cd8839725cee736f2bf75cfbe1839bbe6eb01aa2b659");
        consensus.utxo = 83;
        consensus.BIP65Height = 84;
        consensus.BIP66Height = 85;
        consensus.DIP0003Height = 86;
        consensus.CSV = 87;
        consensus.DIP0003EnforcementHeight = 89;
        consensus.nxt = 90;
        consensus.nEvoSporkStartBlock = 91;
        consensus.nLelantusStartBlock = 100;
        consensus.no_zero_payee = 20000;
        consensus.DIP0008Height = INT_MAX;
        consensus.nEvoSporkStopBlock = INT_MAX;
        consensus.nEvoMasternodeMinimumConfirmations = 35;
        consensus.evoSporkKeyID = "XFWzf2xwwARUY3fLhY83P4TDh2pSybUQ8y";
        consensus.new_version = INT_MAX;

        // reorg
        consensus.nMaxReorgDepth = 5;

        // whitelist
        consensus.txidWhitelist.insert(uint256S("0x0"));

        // Dandelion related values.
        consensus.nDandelionEmbargoMinimum = DANDELION_EMBARGO_MINIMUM;
        consensus.nDandelionEmbargoAvgAdd = DANDELION_EMBARGO_AVG_ADD;
        consensus.nDandelionMaxDestinations = DANDELION_MAX_DESTINATIONS;
        consensus.nDandelionShuffleInterval = DANDELION_SHUFFLE_INTERVAL;
        consensus.nDandelionFluff = DANDELION_FLUFF;

        // Bip39
        consensus.nMnemonicBlock = 1;

        // long living quorum params
        consensus.llmqs[Consensus::LLMQ_50_60] = llmq50_60;
        consensus.llmqs[Consensus::LLMQ_400_60] = llmq400_60;
        consensus.llmqs[Consensus::LLMQ_400_85] = llmq400_85;
        consensus.nLLMQPowTargetSpacing = 5*60;
        consensus.llmqChainLocks = Consensus::LLMQ_400_60;
        consensus.llmqForInstantSend = Consensus::LLMQ_50_60;
        consensus.nInstantSendConfirmationsRequired = 2;
        consensus.nInstantSendKeepLock = 24;
        consensus.nInstantSendBlockFilteringStartHeight = 1000;

        nMaxTipAge = 24 * 60 * 60;
        pchMessageStart[0] = { '0' };
        pchMessageStart[1] = { 'b' };
        pchMessageStart[2] = { 'z' };
        pchMessageStart[3] = { 'x' };
        nDefaultPort = 29301;  //nRPCPort = 29201;
        nPruneAfterHeight = 100000;
        genesis = CreateGenesisBlock(1635237027, 560830, 0x1e0ffff0, 3, 0);
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x000004d916d2e66f820fc0ba0b2554b6491a9c5bfc026ea515df5df2ceafcd53"));
        assert(genesis.hashMerkleRoot == uint256S("0xfede7817612c884cc527b1598013f6ef1feceea08bb80e1ffb0765dd74ba6a53"));
        vSeeds.push_back(CDNSSeedData("51.91.156.251", "51.91.156.251", false));
        vSeeds.push_back(CDNSSeedData("51.91.156.249", "51.91.156.249", false));
        vSeeds.push_back(CDNSSeedData("bzx.pool4u.net:29149","bzx.pool4u.net:29149", false));
        base58Prefixes[PUBKEY_ADDRESS] = std::vector < unsigned char > (1, 75);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector < unsigned char > (1, 34);
        base58Prefixes[SECRET_KEY] = std::vector < unsigned char > (1, 210);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container < std::vector < unsigned char > > ();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container < std::vector < unsigned char > > ();
        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fMiningRequiresPeers = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = true;

        checkpointData = {
            {
                {     0, uint256S("0x000004d916d2e66f820fc0ba0b2554b6491a9c5bfc026ea515df5df2ceafcd53")},
                {    82, uint256S("0x00000811f8cba96565559992346f0074a112cf96de4d17a75e29433e1ed29994")},
                { 11897, uint256S("0xa80d7942d6bc33d66bf971c7169f6900a91b597a6a703b024be0c1e845a580e7")},
                { 44297, uint256S("0x4cade50721b2c6a20ac60d61dc88aeef8e1a3d73f43a01b9325f9c65427b8e05")},
                {125169, uint256S("0xd1e22bc4bdc42e4d9208cd8839725cee736f2bf75cfbe1839bbe6eb01aa2b659")},

            }
        };

        chainTxData = ChainTxData{
                1668588535, // * UNIX timestamp of last checkpoint block
                250000,     // * total number of transactions between genesis and last checkpoint
                            //   (the tx=... number in the SetBestChain debug.log lines)
                1       // * estimated number of transactions per second after checkpoint
        };

        consensus.nMaxLelantusInputPerBlock = 200;
        consensus.nMaxValueLelantusSpendPerBlock = 120000 * COIN;
        consensus.nMaxLelantusInputPerTransaction = 100;
        consensus.nMaxValueLelantusSpendPerTransaction = 100100 * COIN;
        consensus.nMaxValueLelantusMint = 100100 * COIN;

        for (const auto& str : lelantus::lelantus_blacklist) {
            GroupElement coin;
            try {
                coin.deserialize(ParseHex(str).data());
            } catch (...) {
                continue;
            }
            consensus.lelantusBlacklist.insert(coin);
        }

        for (const auto& str : sigma::sigma_blacklist) {
            GroupElement coin;
            try {
                coin.deserialize(ParseHex(str).data());
            } catch (...) {
                continue;
            }
            consensus.sigmaBlacklist.insert(coin);
        }
    }
};

static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
    }
};

static CTestNetParams testNetParams;

/**
 * Devnet (testnet for experimental stuff)
 */
class CDevNetParams : public CChainParams {
public:
    CDevNetParams() {
        strNetworkID = "dev";
    }
};

static CDevNetParams devNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    CRegTestParams() {
        strNetworkID = "regtest";
    }
};
static CRegTestParams regTestParams;

static CChainParams *pCurrentParams = 0;

const CChainParams &Params() {
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams& Params(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
            return mainParams;
    else if (chain == CBaseChainParams::TESTNET)
            return testNetParams;
    else if (chain == CBaseChainParams::DEVNET)
            return devNetParams;
    else if (chain == CBaseChainParams::REGTEST)
            return regTestParams;
    else
        throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}
