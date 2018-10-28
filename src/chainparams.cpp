// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2018 Oxid developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "libzerocoin/Params.h"
#include "random.h"
#include "util.h"
#include "utilstrencodings.h"
#include <assert.h>
#include <boost/assign/list_of.hpp>

using namespace std;
using namespace boost::assign;

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

#include "chainparamsseeds.h"

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
    genesis.nTime = nTime;
    genesis.nBits = nBits;
    genesis.nNonce = nNonce;
    genesis.nVersion = nVersion;
    genesis.vtx.push_back(txNew);
    genesis.hashPrevBlock = 0;
    genesis.hashMerkleRoot = genesis.BuildMerkleTree();
    return genesis;
}

static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nNonce, uint32_t nBits)
{
    int32_t nVersion = 1;
    const char* pszTimestamp = "Oxid 2.0 - starting a new blockchain";
    const CScript genesisOutputScript = CScript() << ParseHex("0472db9ee5e9b5e12d60d4661394a37b4d99d527acd95454e0783c7eb43847c803ba95eb7bfc22138680d3641b2b96f186656b7c7362b00afa01a255ff47cef04b") << OP_CHECKSIG;
    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nNonce, nBits, nVersion, 0 * COIN);
}

/**
 * Main network
 */

// Convert the pnSeeds6 array into usable address objects.
static void convertSeed6(std::vector<CAddress>& vSeedsOut, const SeedSpec6* data, unsigned int count)
{
    const int64_t nOneWeek = 7 * 24 * 60 * 60;
    for (unsigned int i = 0; i < count; i++) {
        struct in6_addr ip;
        memcpy(&ip, data[i].addr, sizeof(ip));
        CAddress addr(CService(ip, data[i].port));
        addr.nTime = GetTime() - GetRand(nOneWeek) - nOneWeek;
        vSeedsOut.push_back(addr);
    }
}

static Checkpoints::MapCheckpoints mapCheckpoints =
    boost::assign::map_list_of
    (0, uint256("0000090d50e2cdb1bda79aae7d9e3b052a0058d66d7af890dd35f97949c012e0"))
    (1000, uint256("0000062c039b02c14960eeeda852d9ef05d3f62239537a9d95f7658b693d7436"));

static const Checkpoints::CCheckpointData data = {
    &mapCheckpoints,
    1540710000, // * UNIX timestamp of last checkpoint block
    1014,       // * total number of transactions between genesis and last checkpoint (the tx=... number in the SetBestChain debug.log lines)
    1000};      // * estimated number of transactions per day after checkpoint

static Checkpoints::MapCheckpoints mapCheckpointsTestnet =
    boost::assign::map_list_of(0, uint256("0x0000010e505bd636c2e300a35f5cd79ed7575163b5f3f37dbb5d6f71e061f324"));
static const Checkpoints::CCheckpointData dataTestnet = {
    &mapCheckpointsTestnet,
    1540711000,
    0,
    250};

static Checkpoints::MapCheckpoints mapCheckpointsRegtest =
    boost::assign::map_list_of(0, uint256("0x00000905393b6a275550951332fb206f0ab078648078757d26da62e40685b58d"));
static const Checkpoints::CCheckpointData dataRegtest = {
    &mapCheckpointsRegtest,
    1540712000,
    0,
    100};

libzerocoin::ZerocoinParams* CChainParams::Zerocoin_Params() const
{
    assert(this);
    static CBigNum bnTrustedModulus(zerocoinModulus);
    static libzerocoin::ZerocoinParams ZCParams = libzerocoin::ZerocoinParams(bnTrustedModulus);
    return &ZCParams;
}


class CMainParams : public CChainParams
{
public:
    CMainParams()
    {
        networkID = CBaseChainParams::MAIN;
        strNetworkID = "main";
        pchMessageStart[0] = 0xa2;
        pchMessageStart[1] = 0xb6;
        pchMessageStart[2] = 0x6d;
        pchMessageStart[3] = 0xba;
        vAlertPubKey = ParseHex("047aae0f18426374a9a3ce7c3fb1ef1519825aa8d1d7c91e3530aff2a054f3280e056b34a4d7f08072fe84203005e3221a965cc3eeaa06346754de152679a4d5f9");
        nDefaultPort = 28932;
        bnProofOfWorkLimit = ~uint256(0) >> 20;
        nMaxReorganizationDepth = 100;
        nEnforceBlockUpgradeMajority = 1006;
        nRejectBlockOutdatedMajority = 1007;
        nToCheckBlockUpgradeMajority = 1008;
        nMinerThreads = 0;
        nTargetTimespan = 120;
        nTargetSpacing = 120;  // Oxid: 2 minute
        nMaturity = 10;
        nMaxMoneyOut = 89000000 * COIN;

        /** Height or Time Based Activations **/
        nLastPOWBlock = 1000;
        nModifierUpdateBlock = 0;             // 0
        nBlockRecalculateAccumulators = 1005; // Trigger a recalculation of accumulators
        nBlockFirstFraudulent = 1003;         // First block that bad serials emerged
        nBlockLastGoodCheckpoint = 1005;      // Last valid accumulator checkpoint
        nBlockEnforceInvalidUTXO = 1001;      // Start enforcing the invalid UTXO's

        /** X11
         * uint32_t nTime  // 1540710000
         * uint32_t nNonce // 2359122
         * uint32_t nBits  // 0x1e0ffff0
         */
        genesis = CreateGenesisBlock(1540710000, 2359122, 0x1e0ffff0);

        /** X11 ALGO:
        Mainnet genesis.GetHash():      0000090d50e2cdb1bda79aae7d9e3b052a0058d66d7af890dd35f97949c012e0
        Mainnet genesis.hashMerkleRoot: dc813bb4d524fb3f8e67ce3481803a22e51ace5554cfce21ba223bcd62c81169
        Mainnet genesis.nTime:          1540710000
        Mainnet genesis.nNonce:         2359122
        */

        hashGenesisBlock = genesis.GetHash();

        assert(hashGenesisBlock == uint256("0x0000090d50e2cdb1bda79aae7d9e3b052a0058d66d7af890dd35f97949c012e0"));
        assert(genesis.hashMerkleRoot == uint256("0xdc813bb4d524fb3f8e67ce3481803a22e51ace5554cfce21ba223bcd62c81169"));

        vSeeds.push_back(CDNSSeedData("104.207.132.149", "104.207.132.149"));
        vSeeds.push_back(CDNSSeedData("45.76.247.235", "45.76.247.235"));
        vSeeds.push_back(CDNSSeedData("seed3.oxid.io", "seed3.oxid.io"));
        vSeeds.push_back(CDNSSeedData("seed4.oxid.io", "seed4.oxid.io"));
        vSeeds.push_back(CDNSSeedData("seed5.oxid.io", "seed5.oxid.io"));
        vSeeds.push_back(CDNSSeedData("seed6.oxid.io", "seed6.oxid.io"));
        vSeeds.push_back(CDNSSeedData("seed7.oxid.io", "seed7.oxid.io"));
        vSeeds.push_back(CDNSSeedData("seed8.oxid.io", "seed8.oxid.io"));
        vSeeds.push_back(CDNSSeedData("seed9.oxid.io", "seed9.oxid.io"));
        vSeeds.push_back(CDNSSeedData("seed10.oxid.io", "seed10.oxid.io"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 115); // o
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 29);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 189);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container<std::vector<unsigned char> >();
        // 	BIP44 coin type is from https://github.com/satoshilabs/slips/blob/master/slip-0044.md
        base58Prefixes[EXT_COIN_TYPE] = boost::assign::list_of(0x13)(0x00)(0x00)(0x80).convert_to_container<std::vector<unsigned char> >();

        convertSeed6(vFixedSeeds, pnSeed6_main, ARRAYLEN(pnSeed6_main));

        fRequireRPCPassword = true;
        fMiningRequiresPeers = true;
        fAllowMinDifficultyBlocks = false;
        fDefaultConsistencyChecks = true;
        fRequireStandard = true;
        fMineBlocksOnDemand = false;
        fSkipProofOfWorkCheck = false;
        fTestnetToBeDeprecatedFieldRPC = false;
        fHeadersFirstSyncingActive = false;

        nPoolMaxTransactions = 3;
        strSporkKey = "0471176c9089e02fc7e4c8bf242817a183915a5d9bf1e9a661d34a55e68d0b5f572976eabbe7d06bff1b795c5ebc0c6856119d65fc5b0bd508272600178415e419";
        strObfuscationPoolDummyAddress = "oKm7rkPs9oN33VdpYo2H3sWckDZtyfeEAK";
        nStartMasternodePayments = 1540710800;

        zerocoinModulus = "25195908475657893494027183240048398571429282126204032027777137836043662020707595556264018525880784"
                          "4069182906412495150821892985591491761845028084891200728449926873928072877767359714183472702618963750149718246911"
                          "6507761337985909570009733045974880842840179742910064245869181719511874612151517265463228221686998754918242243363"
                          "7259085141865462043576798423387184774447920739934236584823824281198163815010674810451660377306056201619676256133"
                          "8441436038339044149526344321901146575444541784240209246165157233507787077498171257724679629263863563732899121548"
                          "31438167899885040445364023527381951378636564391212010397122822120720357";
    }

    const Checkpoints::CCheckpointData& Checkpoints() const
    {
        return data;
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CMainParams
{
public:
    CTestNetParams()
    {
        networkID = CBaseChainParams::TESTNET;
        strNetworkID = "test";
        pchMessageStart[0] = 0x47;
        pchMessageStart[1] = 0x76;
        pchMessageStart[2] = 0x65;
        pchMessageStart[3] = 0xba;
        vAlertPubKey = ParseHex("047aae0f18426374a9a3ce7c3fb1ef1519825aa8d1d7c91e3530aff2a054f3280e056b34a4d7f08072fe84203005e3221a965cc3eeaa06346754de152679a4d5f9");
        nDefaultPort = 28942;
        nEnforceBlockUpgradeMajority = 51;
        nRejectBlockOutdatedMajority = 75;
        nToCheckBlockUpgradeMajority = 100;
        nMinerThreads = 0;
        nTargetTimespan = 1 * 60; // Oxid: 1 day
        nTargetSpacing = 1 * 60;  // Oxid: 1 minute
        nLastPOWBlock = 100;
        nMaturity = 15;
        nModifierUpdateBlock = 0;
        nMaxMoneyOut = 89000000 * COIN;

        /** X11
         * uint32_t nTime  // 1540711000
         * uint32_t nNonce // 72240
         * uint32_t nBits  // 0x1e0ffff0
         */
        genesis = CreateGenesisBlock(1540711000, 72240, 0x1e0ffff0);

        /* X11
        Testnet genesis.GetHash():      0000010e505bd636c2e300a35f5cd79ed7575163b5f3f37dbb5d6f71e061f324
        Testnet genesis.hashMerkleRoot: dc813bb4d524fb3f8e67ce3481803a22e51ace5554cfce21ba223bcd62c81169
        Testnet genesis.nTime:          1540711000
        Testnet genesis.nNonce:         72240
        */

        hashGenesisBlock = genesis.GetHash();

        assert(hashGenesisBlock == uint256("0x0000010e505bd636c2e300a35f5cd79ed7575163b5f3f37dbb5d6f71e061f324"));
        assert(genesis.hashMerkleRoot == uint256("0xdc813bb4d524fb3f8e67ce3481803a22e51ace5554cfce21ba223bcd62c81169"));

        vFixedSeeds.clear();
        vSeeds.clear();

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 177); // Testnet Oxid addresses start with 'x' or 'y'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 18);  // Testnet Oxid script addresses start with '8' or '9'
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 59);      // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        // Testnet Oxid BIP32 pubkeys start with 'DRKV'
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x35)(0x87)(0xCF).convert_to_container<std::vector<unsigned char> >();
        // Testnet Oxid BIP32 prvkeys start with 'DRKP'
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x35)(0x83)(0x94).convert_to_container<std::vector<unsigned char> >();
        // Testnet Oxid BIP44 coin type is '1' (All coin's testnet default)
        base58Prefixes[EXT_COIN_TYPE] = boost::assign::list_of(0x01)(0x00)(0x00)(0x80).convert_to_container<std::vector<unsigned char> >();

        convertSeed6(vFixedSeeds, pnSeed6_test, ARRAYLEN(pnSeed6_test));

        fRequireRPCPassword = true;
        fMiningRequiresPeers = true;
        fAllowMinDifficultyBlocks = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false;
        fTestnetToBeDeprecatedFieldRPC = true;

        nPoolMaxTransactions = 2;
        strSporkKey = "0471176c9089e02fc7e4c8bf242817a183915a5d9bf1e9a661d34a55e68d0b5f572976eabbe7d06bff1b795c5ebc0c6856119d65fc5b0bd508272600178415e419";
        strObfuscationPoolDummyAddress = "VGbik3FETjcw3BVNXDCvQjiCa1hnAJWQzH";
        nStartMasternodePayments = 1540711800;
    }
    const Checkpoints::CCheckpointData& Checkpoints() const
    {
        return dataTestnet;
    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CTestNetParams
{
public:
    CRegTestParams()
    {
        networkID = CBaseChainParams::REGTEST;
        strNetworkID = "regtest";
        strNetworkID = "regtest";
        pchMessageStart[0] = 0xa2;
        pchMessageStart[1] = 0xcf;
        pchMessageStart[2] = 0x7e;
        pchMessageStart[3] = 0xac;
        nEnforceBlockUpgradeMajority = 750;
        nRejectBlockOutdatedMajority = 950;
        nToCheckBlockUpgradeMajority = 1000;
        nMinerThreads = 1;
        nTargetTimespan = 24 * 60 * 60; // Oxid: 1 day
        nTargetSpacing = 1 * 60;        // Oxid: 1 minutes
        bnProofOfWorkLimit = ~uint256(0) >> 1;
        nDefaultPort = 28944;

        /** X11
         * uint32_t nTime  // 1540712000
         * uint32_t nNonce // 1130508
         * uint32_t nBits  // 0x1e0ffff0
         */
        genesis = CreateGenesisBlock(1540712000, 1130508, 0x1e0ffff0);

        /* X11
        Regtest genesis.GetHash():      00000905393b6a275550951332fb206f0ab078648078757d26da62e40685b58d
        Regtest genesis.hashMerkleRoot: dc813bb4d524fb3f8e67ce3481803a22e51ace5554cfce21ba223bcd62c81169
        Regtest genesis.nTime:          1540712000
        Regtest genesis.nNonce:         1130508
        */

        hashGenesisBlock = genesis.GetHash();

        assert(hashGenesisBlock == uint256("0x00000905393b6a275550951332fb206f0ab078648078757d26da62e40685b58d"));

        vFixedSeeds.clear(); //! Testnet mode doesn't have any fixed seeds.
        vSeeds.clear();      //! Testnet mode doesn't have any DNS seeds.

        fRequireRPCPassword = false;
        fMiningRequiresPeers = false;
        fAllowMinDifficultyBlocks = true;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;
    }
    const Checkpoints::CCheckpointData& Checkpoints() const
    {
        return dataRegtest;
    }
};
static CRegTestParams regTestParams;

/**
 * Unit test
 */
class CUnitTestParams : public CMainParams, public CModifiableParams
{
public:
    CUnitTestParams()
    {
        networkID = CBaseChainParams::UNITTEST;
        strNetworkID = "unittest";
        nDefaultPort = 28945;
        vFixedSeeds.clear(); //! Unit test mode doesn't have any fixed seeds.
        vSeeds.clear();      //! Unit test mode doesn't have any DNS seeds.

        fRequireRPCPassword = false;
        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fAllowMinDifficultyBlocks = false;
        fMineBlocksOnDemand = true;
    }

    const Checkpoints::CCheckpointData& Checkpoints() const
    {
        // UnitTest share the same checkpoints as MAIN
        return data;
    }

    //! Published setters to allow changing values in unit test cases
    virtual void setEnforceBlockUpgradeMajority(int anEnforceBlockUpgradeMajority) { nEnforceBlockUpgradeMajority = anEnforceBlockUpgradeMajority; }
    virtual void setRejectBlockOutdatedMajority(int anRejectBlockOutdatedMajority) { nRejectBlockOutdatedMajority = anRejectBlockOutdatedMajority; }
    virtual void setToCheckBlockUpgradeMajority(int anToCheckBlockUpgradeMajority) { nToCheckBlockUpgradeMajority = anToCheckBlockUpgradeMajority; }
    virtual void setDefaultConsistencyChecks(bool afDefaultConsistencyChecks) { fDefaultConsistencyChecks = afDefaultConsistencyChecks; }
    virtual void setAllowMinDifficultyBlocks(bool afAllowMinDifficultyBlocks) { fAllowMinDifficultyBlocks = afAllowMinDifficultyBlocks; }
    virtual void setSkipProofOfWorkCheck(bool afSkipProofOfWorkCheck) { fSkipProofOfWorkCheck = afSkipProofOfWorkCheck; }
};
static CUnitTestParams unitTestParams;


static CChainParams* pCurrentParams = 0;

CModifiableParams* ModifiableParams()
{
    assert(pCurrentParams);
    assert(pCurrentParams == &unitTestParams);
    return (CModifiableParams*)&unitTestParams;
}

const CChainParams& Params()
{
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams& Params(CBaseChainParams::Network network)
{
    switch (network) {
    case CBaseChainParams::MAIN:
        return mainParams;
    case CBaseChainParams::TESTNET:
        return testNetParams;
    case CBaseChainParams::REGTEST:
        return regTestParams;
    case CBaseChainParams::UNITTEST:
        return unitTestParams;
    default:
        assert(false && "Unimplemented network");
        return mainParams;
    }
}

void SelectParams(CBaseChainParams::Network network)
{
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}

bool SelectParamsFromCommandLine()
{
    CBaseChainParams::Network network = NetworkIdFromCommandLine();
    if (network == CBaseChainParams::MAX_NETWORK_TYPES)
        return false;

    SelectParams(network);
    return true;
}
