// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/merkle.h>
#include <masternodes/mn_checks.h>
#include <streams.h>
#include <tinyformat.h>
#include <util/system.h>
#include <util/strencodings.h>
#include <versionbitsinfo.h>

#include <cassert>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/case_conv.hpp>

std::vector<CTransactionRef> CChainParams::CreateGenesisMasternodes()
{
    std::vector<CTransactionRef> mnTxs;
    for (auto const & addrs : vMasternodes)
    {
        CMutableTransaction txNew;
        txNew.nVersion = 1;
        txNew.vin.resize(1);
        txNew.vout.resize(2);
        txNew.vin[0].scriptSig = CScript(); // << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));

        CTxDestination operatorDest = DecodeDestination(addrs.operatorAddress, *this);
        assert(operatorDest.which() == PKHashType || operatorDest.which() == WitV0KeyHashType);
        CTxDestination ownerDest = DecodeDestination(addrs.ownerAddress, *this);
        assert(ownerDest.which() == PKHashType || ownerDest.which() == WitV0KeyHashType);

        CKeyID operatorAuthKey = operatorDest.which() == PKHashType ? CKeyID(*boost::get<PKHash>(&operatorDest)) : CKeyID(*boost::get<WitnessV0KeyHash>(&operatorDest)) ;
        genesisTeam.insert(operatorAuthKey);
        CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
        metadata << static_cast<unsigned char>(CustomTxType::CreateMasternode)
                 << static_cast<char>(operatorDest.which()) << operatorAuthKey;

        CScript scriptMeta;
        scriptMeta << OP_RETURN << ToByteVector(metadata);

        txNew.vout[0] = CTxOut(consensus.mn.creationFee, scriptMeta);
        txNew.vout[1] = CTxOut(consensus.mn.collateralAmount, GetScriptForDestination(ownerDest));

        mnTxs.push_back(MakeTransactionRef(std::move(txNew)));
    }
    return mnTxs;
}

static CBlock CreateGenesisBlock(const char* pszTimestamp, uint32_t nTime, uint32_t nBits, int32_t nVersion, const std::vector<CTxOut> & initdist, std::vector<CTransactionRef> const & extraTxs)
{
    CMutableTransaction txNew;
    txNew.nVersion = 1;
    txNew.vin.resize(1);
    txNew.vout = initdist;
    txNew.vin[0].scriptSig = CScript() << 0 << 486604799 << CScriptNum(4) << std::vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));

    CBlock genesis;
    genesis.nTime           = nTime;
    genesis.nBits           = nBits;
    genesis.nVersion        = nVersion;
    genesis.height          = 0;
    genesis.stakeModifier   = uint256S("0");
    genesis.mintedBlocks    = 0;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));

    for (auto tx : extraTxs)
    {
        genesis.vtx.push_back(tx);
    }

    genesis.hashPrevBlock.SetNull();
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

/**
 * Build the genesis block. Note that the output of its generation
 * transaction cannot be spent since it did not originally exist in the
 * database.
 *
 * CBlock(hash=000000000019d6, ver=1, hashPrevBlock=00000000000000, hashMerkleRoot=4a5e1e, nTime=1231006505, nBits=1d00ffff, vtx=1)
 *   CTransaction(hash=4a5e1e, ver=1, vin.size=1, vout.size=1, nLockTime=0)
 *     CTxIn(COutPoint(000000, -1), coinbase 04ffff001d0104455468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f6e64206261696c6f757420666f722062616e6b73)
 *     CTxOut(nValue=50.00000000, scriptPubKey=0x5F1DF16B2B704C8A578D0B)
 *   vMerkleTree: 4a5e1e
 */
static CBlock CreateGenesisBlock(uint32_t nTime, uint32_t nBits, int32_t nVersion, const std::vector<CTxOut> & initdist, std::vector<CTransactionRef> const & extraTxs)
{
    const char* pszTimestamp = "Financial Times 23/Mar/2020 The Federal Reserve has gone well past the point of ‘QE infinity’";
//    const CScript genesisOutputScript = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
//    return CreateGenesisBlock(pszTimestamp, genesisOutputScript, nTime, nBits, nVersion, genesisReward, extraTxs);
    return CreateGenesisBlock(pszTimestamp, nTime, nBits, nVersion, initdist, extraTxs);
}

/**
 * Main network
 */
class CMainParams : public CChainParams {
public:
    CMainParams() {
        strNetworkID = "main";
        consensus.nSubsidyHalvingInterval = 210000; /// @attention totally disabled for main
        consensus.baseBlockSubsidy = 200 * COIN;
        consensus.newBaseBlockSubsidy = 40504000000; // 405.04 DFI
        consensus.emissionReductionPeriod = 32690; // Two weeks
        consensus.emissionReductionAmount = 1658; // 1.658%
        consensus.BIP16Exception = uint256(); //("0x00000000000002dc756eebf4f49723ed8d30cc28a5f108eb94b1ba88ac4f9c22");
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 0; // 000000000000000004c2b624ed5d7756c508d90fd0da2c7c679febfa6c4735f0
        consensus.BIP66Height = 0; // 00000000000000000379eaa19dce8c9b722d46ae6a57c2f1a988119488b50931
        consensus.AMKHeight = 356500;
        consensus.BayfrontHeight = 405000;
        consensus.BayfrontMarinaHeight = 465150;
        consensus.BayfrontGardensHeight = 488300;
        consensus.ClarkeQuayHeight = 595738;
        consensus.DakotaHeight = 678000; // 1st March 2021
        consensus.DakotaCrescentHeight = 733000; // 25th March 2021
        consensus.EunosHeight = 894000; // 3rd June 2021
        consensus.EunosKampungHeight = 895743;
        consensus.EunosPayaHeight = 1072000; // Aug 05, 2021.
        consensus.FortCanningHeight = std::numeric_limits<int>::max();

        consensus.pos.diffLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
//        consensus.pos.nTargetTimespan = 14 * 24 * 60 * 60; // two weeks
//        consensus.pos.nTargetSpacing = 10 * 60; // 10 minutes
        consensus.pos.nTargetTimespan = 5 * 60; // 5 min == 10 blocks
        consensus.pos.nTargetSpacing = 30; // seconds
        consensus.pos.nTargetTimespanV2 = 1008 * consensus.pos.nTargetSpacing; // 1008 blocks
        consensus.pos.nStakeMinAge = 0;
        consensus.pos.nStakeMaxAge = 14 * 24 * 60 * 60; // Two weeks
        consensus.pos.fAllowMinDifficultyBlocks = false; // only for regtest
        consensus.pos.fNoRetargeting = false; // only for regtest

        consensus.pos.allowMintingWithoutPeers = false; // don't mint if no peers connected

        consensus.CSVHeight = 1; // 000000000000000004a1b34462cb8aeebd5799177f7a29cf28f2d1961716b5b5
        consensus.SegwitHeight = 0; // 0000000000000000001c8018d9cb3b742ef25114f27563e3fc4a1902167f9893
        consensus.nRuleChangeActivationThreshold = 9; //1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 10; //2016; // nTargetTimespan / nTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x000000000000000000000000000000000000000000003f2949bfe4efc275390c");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x9b257cb88630e422902ef2b17a3627ae2f786a5923df9c3bda4226f9551b1ea8");

        // Masternodes' params
        consensus.mn.activationDelay = 10;
        consensus.mn.newActivationDelay = 1008;
        consensus.mn.resignDelay = 60;
        consensus.mn.newResignDelay = 2 * consensus.mn.newActivationDelay;
        consensus.mn.creationFee = 10 * COIN;
        consensus.mn.collateralAmount = 1000000 * COIN;
        consensus.mn.collateralAmountDakota = 20000 * COIN;
        consensus.mn.anchoringTeamSize = 5;
        consensus.mn.anchoringFrequency = 15;

        consensus.mn.anchoringTimeDepth = 3 * 60 * 60; // 3 hours
        consensus.mn.anchoringAdditionalTimeDepth = 1 * 60 * 60; // 1 hour
        consensus.mn.anchoringTeamChange = 120; // Number of blocks

        consensus.token.creationFee = 100 * COIN;
        consensus.token.collateralAmount = 1 * COIN;

        consensus.spv.creationFee = 100000; // should be > bitcoin's dust
        consensus.spv.anchorSubsidy = 0 * COIN;
        consensus.spv.subsidyIncreasePeriod = 60;
        consensus.spv.subsidyIncreaseValue = 5 * COIN;
        consensus.spv.wallet_xpub = "xpub68vVWYqkpwYT8ZxBhN2buFMTPNFzrJQV19QZmhuwQqKQZHxcXVg36GZCrwPhb7KPpivsGXxvd7g82sJXYnKNqi2ZuHJvhqcwF418YEfGMrv";
        consensus.spv.anchors_address = "1FtZwEZKknoquUb6DyQHFZ6g6oomXJYEcb";
        consensus.spv.minConfirmations = 6;

        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::IncentiveFunding, 45 * COIN / 200); // 45 DFI of 200 per block (rate normalized to (COIN == 100%))
        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::AnchorReward, COIN /10 / 200);       // 0.1 DFI of 200 per block

        // New coinbase reward distribution
        consensus.dist.masternode = 3333; // 33.33%
        consensus.dist.community = 491; // 4.91%
        consensus.dist.anchor = 2; // 0.02%
        consensus.dist.liquidity = 2545; // 25.45%
        consensus.dist.swap = 1234; // 12.34%
        consensus.dist.futures = 1234; // 12.34%
        consensus.dist.options = 988; // 9.88%
        consensus.dist.unallocated = 173; // 1.73%

        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::AnchorReward, consensus.dist.anchor);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::IncentiveFunding, consensus.dist.liquidity);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Swap, consensus.dist.swap);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Futures, consensus.dist.futures);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Options, consensus.dist.options);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Unallocated, consensus.dist.unallocated);

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xf9;
        pchMessageStart[1] = 0xbe;
        pchMessageStart[2] = 0xb4;
        pchMessageStart[3] = 0xd9;
        pchMessageStartPostAMK[0] = 0xe2;
        pchMessageStartPostAMK[1] = 0xaa;
        pchMessageStartPostAMK[2] = 0xc1;
        pchMessageStartPostAMK[3] = 0xe1;
        nDefaultPort = 8555;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 240;
        m_assumed_chain_state_size = 3;

        base58Prefixes[PUBKEY_ADDRESS] = {0x12}; // '8' (0('1') for bitcoin)
        base58Prefixes[SCRIPT_ADDRESS] = {0x5a}; // 'd' (5('3') for bitcoin)
        base58Prefixes[SECRET_KEY] =     {0x80}; // (128 ('5', 'K' or 'L') for bitcoin)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "df";

        // (!) after prefixes set
        consensus.foundationShareScript = GetScriptForDestination(DecodeDestination("dZcHjYhKtEM88TtZLjp314H2xZjkztXtRc", *this));
        consensus.foundationShare = 10; // old style - just percents
        consensus.foundationShareDFIP1 = 199 * COIN / 10 / 200; // 19.9 DFI @ 200 per block (rate normalized to (COIN == 100%)

        consensus.foundationMembers.clear();
        consensus.foundationMembers.insert(GetScriptForDestination(DecodeDestination("dJEbxbfufyPF14SC93yxiquECEfq4YSd9L", *this)));
        consensus.foundationMembers.insert(GetScriptForDestination(DecodeDestination("8bL7jZe2Nk5EhqFA6yuf8HPre3M6eewkqj", *this)));
        consensus.foundationMembers.insert(GetScriptForDestination(DecodeDestination("8UhqhhiwtUuEqCD7HsekUsgYRuz115eLiQ", *this)));

        consensus.accountDestruction.clear();
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("dJEbxbfufyPF14SC93yxiquECEfq4YSd9L", *this)));
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("8UAhRuUFCyFUHEPD7qvtj8Zy2HxF5HH5nb", *this)));

        // owner base58, operator base58
        vMasternodes.push_back({"8PuErAcazqccCVzRcc8vJ3wFaZGm4vFbLe", "8J846CKFF83Jcj5m4EReJmxiaJ6Jy1Y6Ea"});
        vMasternodes.push_back({"8RPZm7SVUNhGN1RgGY3R92rvRkZBwETrCX", "8bzHwhaF2MaVs4owRvpWtZQVug3mKuJji2"});
        vMasternodes.push_back({"8KRsoeCRKHUFFmAGGJbRBAgraXiUPUVuXn", "8cHaEaqRsz7fgW1eAjeroB5Bau5NfJNbtk"});

        std::vector<CTxOut> initdist;
        initdist.push_back(CTxOut(58800000 * COIN, GetScriptForDestination(DecodeDestination("8ZWWN1nX8drxJBSMG1VS9jH4ciBSvA9nxp", *this))));
        initdist.push_back(CTxOut(44100000 * COIN, GetScriptForDestination(DecodeDestination("8aGPBahDX4oAXx9okpGRzHPS3Td1pZaLgU", *this))));
        initdist.push_back(CTxOut(11760000 * COIN, GetScriptForDestination(DecodeDestination("8RGSkdaft9EmSXXp6b2UFojwttfJ5BY29r", *this))));
        initdist.push_back(CTxOut(11760000 * COIN, GetScriptForDestination(DecodeDestination("8L7qGjjHRa3Agks6incPomWCfLSMPYipmU", *this))));
        initdist.push_back(CTxOut(29400000 * COIN, GetScriptForDestination(DecodeDestination("dcZ3NXrpbNWvx1rhiGvXStM6EQtHLc44c9", *this))));
        initdist.push_back(CTxOut(14700000 * COIN, GetScriptForDestination(DecodeDestination("dMty9CfknKEaXqJuSgYkvvyF6UB6ffrZXG", *this))));
        initdist.push_back(CTxOut(64680000 * COIN, GetScriptForDestination(DecodeDestination("dZcY1ZNm5bkquz2J74smKqokuPoVpPvGWu", *this))));
        initdist.push_back(CTxOut(235200000 * COIN, GetScriptForDestination(DecodeDestination("dP8dvN5pnwbsxFcfN9DyqPVZi1fVHicDd2", *this))));
        initdist.push_back(CTxOut(117600000 * COIN, GetScriptForDestination(DecodeDestination("dMs1xeSGZbGnTJWqTwjR4mcjp2egpEXG6M", *this))));
        {
            CAmount sum_initdist{0};
            for (CTxOut const & out : initdist)
                sum_initdist += out.nValue;
            assert(sum_initdist == 588000000 * COIN);
        }

        consensus.burnAddress = GetScriptForDestination(DecodeDestination("8defichainBurnAddressXXXXXXXdRQkSm", *this));
        consensus.retiredBurnAddress = GetScriptForDestination(DecodeDestination("8defichainDSTBurnAddressXXXXaCAuTq", *this));

        genesis = CreateGenesisBlock(1587883831, 0x1d00ffff, 1, initdist, CreateGenesisMasternodes()); // old=1231006505
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("0x279b1a87aedc7b9471d4ad4e5f12967ab6259926cd097ade188dfcf22ebfe72a"));
        assert(genesis.hashMerkleRoot == uint256S("0x03d771953b10d3506b3c3d9511e104d715dd29279be4b072ffc5218bb18adacf"));

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
        vSeeds.emplace_back("seed.defichain.io");
        vSeeds.emplace_back("seed.mydeficha.in");

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = false;

        checkpointData = {
            {
                {     0, consensus.hashGenesisBlock},
                { 50000, uint256S("a45e6bf6ae858a287eb39021ea23880b4115c94e882e2b7c0fcfc98c317922cd")},
                {100000, uint256S("3acd556dbd5e6e75bf463a15eeeeb54b6eab4a1f28039bdc343cc8c851cce45c")},
                {150000, uint256S("46b231d42e5b002852708d48dec119bbc2d550fb67908f1e9f35102c1b45b94d")},
                {200000, uint256S("414076e74894aaed3e1b52d64937f23289d59fe80e287c328a1281398bf9cb31")},
                {250000, uint256S("d50a44503fa55cd01a78b98dea125e63b65aac720c96cca696857722e8149d77")},
                {300000, uint256S("351c82cb8f77fba73e24223a9dd50954630560602c3a38f4d1c03dfa5cf1fd10")},
                {350000, uint256S("ebc8737cb2caa77397f446e9a5aff72a2ca9e8305a6a6f8eb4b6c22f389bef08")},
                {400000, uint256S("97c1014a66c9f327e04a59b3e1b4f551122d0698b6b1a98ec99555fffb474e9d")},
                {450000, uint256S("03701a440b02d61b875ba2503bb53f1f1360cf66b4f0cf472e660a6809534379")},
                {500000, uint256S("6a5b285bc68362deb66148069f55f82c02974056e73f5cc96971f7661ecd5880")},
                {550000, uint256S("3f9aab70727d3cc76a3d406f520a71ccc6095aeea2d185e489f563320d429d5b")},
                {597925, uint256S("0ff2aa3749300e3d0b5bc8d48f9d699bc42e222fe718dc011b33913127087c6d")},
                {600000, uint256S("79ddf4537e40cb59335a0551e5edc7bd396e6949aa2864c3200ca66f9c455405")},
                {650000, uint256S("f18d64dd75c53590e833d3068132a65644963d5c5aebb4c73d42cbde8dc28d68")},
                {757420, uint256S("8d4918be2b2df30175f9e611d9ceb494215b93f2267075ace3f031e784cbccbe")},
                {850000, uint256S("2d7d58ae18a74f73b9836a8fffd3f65ce409536e654a6c644ce735215238a004")},
                {875000, uint256S("44d3b3ba8e920cef86b7ec096ab0a2e608d9fedc14a59611a76a5e40aa53145e")},
                {895741, uint256S("61bc1d73c720990dde43a3fec1f703a222ec5c265e6d491efd60eeec1bdb6dc3")},
            }
        };

        chainTxData = ChainTxData{
            // Data from rpc: getchaintxstats 4096 04aed18435a87754fcccb32734a02cf9ee162292489a476334326e8cf8a1079f
            /* nTime    */ 1611229003,
            /* nTxCount */ 1091894,
            /* dTxRate  */ 0.1841462153145931
        };
    }
};

/**
 * Testnet (v3)
 */
class CTestNetParams : public CChainParams {
public:
    CTestNetParams() {
        strNetworkID = "test";
        consensus.nSubsidyHalvingInterval = 210000; /// @attention totally disabled for testnet
        consensus.baseBlockSubsidy = 200 * COIN;
        consensus.newBaseBlockSubsidy = 40504000000;
        consensus.emissionReductionPeriod = 32690; // Two weeks
        consensus.emissionReductionAmount = 1658; // 1.658%
        consensus.BIP16Exception = uint256(); //("0x00000000dd30457c001f4095d208cc1296b0eed002427aa599874af7a432b105");
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 0; // 00000000007f6655f22f98e72ed80d8b06dc761d5da09df0fa1dc4be4f861eb6
        consensus.BIP66Height = 0; // 000000002104c8c45e99a8853285a3b592602a3ccde2b832481da85e9e4ba182
        consensus.AMKHeight = 150;
        consensus.BayfrontHeight = 3000;
        consensus.BayfrontMarinaHeight = 90470;
        consensus.BayfrontGardensHeight = 101342;
        consensus.ClarkeQuayHeight = 155000;
        consensus.DakotaHeight = 220680;
        consensus.DakotaCrescentHeight = 287700;
        consensus.EunosHeight = 354950;
        consensus.EunosKampungHeight = consensus.EunosHeight;
        consensus.EunosPayaHeight = 463300;
        consensus.FortCanningHeight = std::numeric_limits<int>::max();

        consensus.pos.diffLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
//        consensus.pos.nTargetTimespan = 14 * 24 * 60 * 60; // two weeks
//        consensus.pos.nTargetSpacing = 10 * 60; // 10 minutes
        consensus.pos.nTargetTimespan = 5 * 60; // 5 min == 10 blocks
        consensus.pos.nTargetSpacing = 30;
        consensus.pos.nTargetTimespanV2 = 1008 * consensus.pos.nTargetSpacing; // 1008 blocks
        consensus.pos.nStakeMinAge = 0;
        consensus.pos.nStakeMaxAge = 14 * 24 * 60 * 60; // Two weeks
        consensus.pos.fAllowMinDifficultyBlocks = false;
        consensus.pos.fNoRetargeting = false; // only for regtest

        consensus.pos.allowMintingWithoutPeers = true;

        consensus.CSVHeight = 1; // 00000000025e930139bac5c6c31a403776da130831ab85be56578f3fa75369bb
        consensus.SegwitHeight = 0; // 00000000002b980fcd729daaa248fd9316a5200e9b367f4ff2c42453e84201ca
        consensus.nRuleChangeActivationThreshold = 8; //1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 10; //2016; // nTargetTimespan / nTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        // Masternodes' params
        consensus.mn.activationDelay = 10;
        consensus.mn.newActivationDelay = 1008;
        consensus.mn.resignDelay = 60;
        consensus.mn.newResignDelay = 2 * consensus.mn.newActivationDelay;
        consensus.mn.creationFee = 10 * COIN;
        consensus.mn.collateralAmount = 1000000 * COIN;
        consensus.mn.collateralAmountDakota = 20000 * COIN;
        consensus.mn.anchoringTeamSize = 5;
        consensus.mn.anchoringFrequency = 15;

        consensus.mn.anchoringTimeDepth = 3 * 60 * 60; // 3 hours
        consensus.mn.anchoringAdditionalTimeDepth = 1 * 60 * 60; // 1 hour
        consensus.mn.anchoringTeamChange = 120; // Number of blocks

        consensus.token.creationFee = 100 * COIN;
        consensus.token.collateralAmount = 1 * COIN;

        consensus.spv.creationFee = 100000; // should be > bitcoin's dust
        consensus.spv.wallet_xpub = "tpubD9RkyYW1ixvD9vXVpYB1ka8rPZJaEQoKraYN7YnxbBxxsRYEMZgRTDRGEo1MzQd7r5KWxH8eRaQDVDaDuT4GnWgGd17xbk6An6JMdN4dwsY";
        consensus.spv.anchors_address = "mpAkq2LyaUvKrJm2agbswrkn3QG9febnqL";
        consensus.spv.anchorSubsidy = 0 * COIN;
        consensus.spv.subsidyIncreasePeriod = 60;
        consensus.spv.subsidyIncreaseValue = 5 * COIN;
        consensus.spv.minConfirmations = 1;

        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::IncentiveFunding, 45 * COIN / 200); // 45 DFI @ 200 per block (rate normalized to (COIN == 100%))
        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::AnchorReward, COIN/10 / 200);       // 0.1 DFI @ 200 per block

        // New coinbase reward distribution
        consensus.dist.masternode = 3333; // 33.33%
        consensus.dist.community = 491; // 4.91%
        consensus.dist.anchor = 2; // 0.02%
        consensus.dist.liquidity = 2545; // 25.45%
        consensus.dist.swap = 1234; // 12.34%
        consensus.dist.futures = 1234; // 12.34%
        consensus.dist.options = 988; // 9.88%
        consensus.dist.unallocated = 173; // 1.73%

        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::AnchorReward, consensus.dist.anchor);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::IncentiveFunding, consensus.dist.liquidity);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Swap, consensus.dist.swap);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Futures, consensus.dist.futures);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Options, consensus.dist.options);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Unallocated, consensus.dist.unallocated);

        pchMessageStartPostAMK[0] = pchMessageStart[0] = 0x0b;
        pchMessageStartPostAMK[1] = pchMessageStart[1] = 0x11;
        pchMessageStartPostAMK[2] = pchMessageStart[2] = 0x09;
        pchMessageStartPostAMK[3] = pchMessageStart[3] = 0x07;

        nDefaultPort = 18555;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 30;
        m_assumed_chain_state_size = 2;

        base58Prefixes[PUBKEY_ADDRESS] = {0xf}; // '7' (111 ('m' or 'n') for bitcoin)
        base58Prefixes[SCRIPT_ADDRESS] = {0x80}; // 't' (196 ('2') for bitcoin)
        base58Prefixes[SECRET_KEY] =     {0xef}; // (239 ('9' or 'c') for bitcoin)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tf";

        // (!) after prefixes set
        consensus.foundationShareScript = GetScriptForDestination(DecodeDestination("7Q2nZCcKnxiRiHSNQtLB27RA5efxm2cE7w", *this));
        consensus.foundationShare = 10; // old style - just percents
        consensus.foundationShareDFIP1 = 199 * COIN / 10 / 200; // 19.9 DFI @ 200 per block (rate normalized to (COIN == 100%)

        consensus.foundationMembers.clear();
        consensus.foundationMembers.insert(consensus.foundationShareScript);

        consensus.accountDestruction.clear();
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("trnZD2qPU1c3WryBi8sWX16mEaq9WkGHeg", *this))); // cVUZfDj1B1o7eVhxuZr8FQLh626KceiGQhZ8G6YCUdeW3CAV49ti
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("75jrurn8tkDLhZ3YPyzhk6D9kc1a4hBrmM", *this))); // cSmsVpoR6dSW5hPNKeGwC561gXHXcksdQb2yAFQdjbSp5MUyzZqr

        // owner base58, operator base58
        vMasternodes.push_back({"7LMorkhKTDjbES6DfRxX2RiNMbeemUkxmp", "7KEu9JMKCx6aJ9wyg138W3p42rjg19DR5D"});
        vMasternodes.push_back({"7E8Cjn9cqEwnrc3E4zN6c5xKxDSGAyiVUM", "78MWNEcAAJxihddCw1UnZD8T7fMWmUuBro"});
        vMasternodes.push_back({"7GxxMCh7sJsvRK4GXLX5Eyh9B9EteXzuum", "7MYdTGv3bv3z65ai6y5J1NFiARg8PYu4hK"});
        vMasternodes.push_back({"7BQZ67KKYWSmVRukgv57m4HorjbGh7NWrQ", "7GULFtS6LuJfJEikByKKg8psscg84jnfHs"});

        std::vector<CTxOut> initdist;
        initdist.push_back(CTxOut(100000000 * COIN, GetScriptForDestination(DecodeDestination("te7wgg1X9HDJvMbrP2S51uz2Gxm2LPW4Gr", *this))));
        initdist.push_back(CTxOut(100000000 * COIN, GetScriptForDestination(DecodeDestination("tmYVkwmcv73Hth7hhHz15mx5K8mzC1hSef", *this))));
        initdist.push_back(CTxOut(100000000 * COIN, GetScriptForDestination(DecodeDestination("tahuMwb9eX83eJhf2vXL6NPzABy3Ca8DHi", *this))));

        consensus.burnAddress = GetScriptForDestination(DecodeDestination("7DefichainBurnAddressXXXXXXXdMUE5n", *this));
        consensus.retiredBurnAddress = GetScriptForDestination(DecodeDestination("7DefichainDSTBurnAddressXXXXXzS4Hi", *this));

        genesis = CreateGenesisBlock(1586099762, 0x1d00ffff, 1, initdist, CreateGenesisMasternodes()); // old=1296688602
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("0x034ac8c88a1a9b846750768c1ad6f295bc4d0dc4b9b418aee5c0ebd609be8f90"));
        assert(genesis.hashMerkleRoot == uint256S("0xb71cfd828e692ca1b27e9df3a859740851047a5b5a68f659a908e8815aa35f38"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
        vSeeds.emplace_back("testnet-seed.defichain.io");

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        m_is_test_chain = true;


        checkpointData = {
            {
                { 50000, uint256S("74a468206b59bfc2667aba1522471ca2f0a4b7cd807520c47355b040c7735ccc")},
                {100000, uint256S("9896ac2c34c20771742bccda4f00f458229819947e02204022c8ff26093ac81f")},
                {150000, uint256S("af9307f438f5c378d1a49cfd3872173a07ed4362d56155e457daffd1061742d4")},
                {300000, uint256S("205b522772ce34206a08a635c800f99d2fc4e9696ab8c470dad7f5fa51dfea1a")},
            }
        };

        chainTxData = ChainTxData{
            // Data from rpc: getchaintxstats 4096 04aed18435a87754fcccb32734a02cf9ee162292489a476334326e8cf8a1079f
            /* nTime    */ 1611229441,
            /* nTxCount */ 178351,
            /* dTxRate  */ 0.03842042178237066
        };
    }
};

/**
 * Devnet
 */
class CDevNetParams : public CChainParams {
public:
    CDevNetParams() {
        strNetworkID = "devnet";
        consensus.nSubsidyHalvingInterval = 210000; /// @attention totally disabled for devnet
        consensus.baseBlockSubsidy = 200 * COIN;
        consensus.newBaseBlockSubsidy = 40504000000;
        consensus.emissionReductionPeriod = 32690; // Two weeks
        consensus.emissionReductionAmount = 1658; // 1.658%
        consensus.BIP16Exception = uint256();
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 0;
        consensus.BIP66Height = 0;
        consensus.AMKHeight = 0;
        consensus.BayfrontHeight = 0;
        consensus.BayfrontMarinaHeight = 0;
        consensus.BayfrontGardensHeight = 0;
        consensus.ClarkeQuayHeight = 0;
        consensus.DakotaHeight = 10;
        consensus.DakotaCrescentHeight = 10;
        consensus.EunosHeight = 150;
        consensus.EunosKampungHeight = consensus.EunosHeight;
        consensus.EunosPayaHeight = 300;
        consensus.FortCanningHeight = std::numeric_limits<int>::max();

        consensus.pos.diffLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.pos.nTargetTimespan = 5 * 60; // 5 min == 10 blocks
        consensus.pos.nTargetSpacing = 30;
        consensus.pos.nTargetTimespanV2 = 1008 * consensus.pos.nTargetSpacing; // 1008 blocks
        consensus.pos.nStakeMinAge = 0;
        consensus.pos.nStakeMaxAge = 14 * 24 * 60 * 60; // Two weeks
        consensus.pos.fAllowMinDifficultyBlocks = false;
        consensus.pos.fNoRetargeting = false; // only for regtest

        consensus.pos.allowMintingWithoutPeers = true;

        consensus.CSVHeight = 1;
        consensus.SegwitHeight = 0;
        consensus.nRuleChangeActivationThreshold = 8; //1512; // 75% for testchains
        consensus.nMinerConfirmationWindow = 10; //2016; // nTargetTimespan / nTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        // Masternodes' params
        consensus.mn.activationDelay = 10;
        consensus.mn.newActivationDelay = 1008;
        consensus.mn.resignDelay = 60;
        consensus.mn.newResignDelay = 2 * consensus.mn.newActivationDelay;
        consensus.mn.creationFee = 10 * COIN;
        consensus.mn.collateralAmount = 1000000 * COIN;
        consensus.mn.collateralAmountDakota = 20000 * COIN;
        consensus.mn.anchoringTeamSize = 5;
        consensus.mn.anchoringFrequency = 15;

        consensus.mn.anchoringTimeDepth = 3 * 60 * 60; // 3 hours
        consensus.mn.anchoringAdditionalTimeDepth = 1 * 60 * 60; // 1 hour
        consensus.mn.anchoringTeamChange = 120; // Number of blocks

        consensus.token.creationFee = 100 * COIN;
        consensus.token.collateralAmount = 1 * COIN;

        consensus.spv.creationFee = 100000; // should be > bitcoin's dust
        consensus.spv.wallet_xpub = "tpubD9RkyYW1ixvD9vXVpYB1ka8rPZJaEQoKraYN7YnxbBxxsRYEMZgRTDRGEo1MzQd7r5KWxH8eRaQDVDaDuT4GnWgGd17xbk6An6JMdN4dwsY"; /// @note devnet matter
        consensus.spv.anchors_address = "mpAkq2LyaUvKrJm2agbswrkn3QG9febnqL"; /// @note devnet matter
        consensus.spv.anchorSubsidy = 0 * COIN;
        consensus.spv.subsidyIncreasePeriod = 60;
        consensus.spv.subsidyIncreaseValue = 5 * COIN;
        consensus.spv.minConfirmations = 1;

        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::IncentiveFunding, 45 * COIN / 200); // 45 DFI @ 200 per block (rate normalized to (COIN == 100%))
        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::AnchorReward, COIN/10 / 200);       // 0.1 DFI @ 200 per block

        // New coinbase reward distribution
        consensus.dist.masternode = 3333; // 33.33%
        consensus.dist.community = 491; // 4.91%
        consensus.dist.anchor = 2; // 0.02%
        consensus.dist.liquidity = 2545; // 25.45%
        consensus.dist.swap = 1234; // 12.34%
        consensus.dist.futures = 1234; // 12.34%
        consensus.dist.options = 988; // 9.88%
        consensus.dist.unallocated = 173; // 1.73%

        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::AnchorReward, consensus.dist.anchor);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::IncentiveFunding, consensus.dist.liquidity);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Swap, consensus.dist.swap);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Futures, consensus.dist.futures);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Options, consensus.dist.options);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Unallocated, consensus.dist.unallocated);

        pchMessageStartPostAMK[0] = pchMessageStart[0] = 0x0b;
        pchMessageStartPostAMK[1] = pchMessageStart[1] = 0x11;
        pchMessageStartPostAMK[2] = pchMessageStart[2] = 0x09;
        pchMessageStartPostAMK[3] = pchMessageStart[3] = 0x07;
        nDefaultPort = 20555; /// @note devnet matter
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 30;
        m_assumed_chain_state_size = 2;

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,15); // '7' (111 ('m' or 'n') for bitcoin)
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,128); // 't' (196 ('2') for bitcoin)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239); // (239 ('9' or 'c') for bitcoin)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "tf";

        // (!) after prefixes set
        consensus.foundationShareScript = GetScriptForDestination(DecodeDestination("7Q2nZCcKnxiRiHSNQtLB27RA5efxm2cE7w", *this));
        consensus.foundationShare = 10; // old style - just percents
        consensus.foundationShareDFIP1 = 199 * COIN / 10 / 200; // 19.9 DFI @ 200 per block (rate normalized to (COIN == 100%)

        // now it is for devnet and regtest only, 2 first of genesis MNs acts as foundation members
        consensus.foundationMembers.emplace(GetScriptForDestination(DecodeDestination("7M3g9CSERjLdXisE5pv2qryDbURUj9Vpi1", *this)));
        consensus.foundationMembers.emplace(GetScriptForDestination(DecodeDestination("7L29itepC13pgho1X2y7mcuf4WjkBi7x2w", *this)));

        // owner base58, operator base58
        vMasternodes.push_back({"7M3g9CSERjLdXisE5pv2qryDbURUj9Vpi1", "7Grgx69MZJ4wDKRx1bBxLqTnU9T3quKW7n"});
        vMasternodes.push_back({"7L29itepC13pgho1X2y7mcuf4WjkBi7x2w", "773MiaEtQK2HAwWj55gyuRiU8tSwowRTTW"});
        vMasternodes.push_back({"75Wramp2iARchHedXcn1qRkQtMpSt9Mi3V", "7Ku81yvqbPkxpWjZpZWZZnWydXyzJozZfN"});
        vMasternodes.push_back({"7LfqHbyh9dBQDjWB6MxcWvH2PBC5iY4wPa", "75q6ftr3QGfBT3DBu15fVfetP6duAgfhNH"});

        std::vector<CTxOut> initdist;
        initdist.push_back(CTxOut(100000000 * COIN, GetScriptForDestination(DecodeDestination("7M3g9CSERjLdXisE5pv2qryDbURUj9Vpi1", *this))));
        initdist.push_back(CTxOut(100000000 * COIN, GetScriptForDestination(DecodeDestination("7L29itepC13pgho1X2y7mcuf4WjkBi7x2w", *this))));
        initdist.push_back(CTxOut(100000000 * COIN, GetScriptForDestination(DecodeDestination("75Wramp2iARchHedXcn1qRkQtMpSt9Mi3V", *this))));
        initdist.push_back(CTxOut(100000000 * COIN, GetScriptForDestination(DecodeDestination("7LfqHbyh9dBQDjWB6MxcWvH2PBC5iY4wPa", *this))));

        consensus.burnAddress = GetScriptForDestination(DecodeDestination("7DefichainBurnAddressXXXXXXXdMUE5n", *this));
        consensus.retiredBurnAddress = GetScriptForDestination(DecodeDestination("7DefichainDSTBurnAddressXXXXXzS4Hi", *this));

        genesis = CreateGenesisBlock(1585132338, 0x1d00ffff, 1, initdist, CreateGenesisMasternodes()); // old=1296688602
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("0x0000099a168f636895a019eacfc1798ec54c593c015cfc5aac1f12817f7ddff7"));
        assert(genesis.hashMerkleRoot == uint256S("0x3f327ba2475176bcf8226b10d871f0f992e17ba9e040ff3dbd11d17c1e5914cb"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
//        vSeeds.emplace_back("testnet-seed.defichain.io");
        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_devnet, pnSeed6_devnet + ARRAYLEN(pnSeed6_devnet));

        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        m_is_test_chain = true;


        checkpointData = {
            {
//                {546, uint256S("000000002a936ca763904c3c35fce2f3556c559c0214345d31b1bcebf76acb70")},
            }
        };

        chainTxData = ChainTxData{
            /* nTime    */ 0,
            /* nTxCount */ 0,
            /* dTxRate  */ 0
        };
    }
};

/**
 * Regression test
 */
class CRegTestParams : public CChainParams {
public:
    explicit CRegTestParams(const ArgsManager& args) {
        strNetworkID = "regtest";
        bool isJellyfish = false;
        isJellyfish = gArgs.GetBoolArg("-jellyfish_regtest", false);
        consensus.nSubsidyHalvingInterval = (isJellyfish) ? 210000 : 150;
        consensus.baseBlockSubsidy = (isJellyfish) ? 100 * COIN : 50 * COIN;
        consensus.newBaseBlockSubsidy = 40504000000;
        consensus.emissionReductionPeriod = (isJellyfish) ? 32690 : 150;
        consensus.emissionReductionAmount = 1658; // 1.658%
        consensus.BIP16Exception = uint256();
        consensus.BIP34Height = 500; // BIP34 activated on regtest (Used in functional tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in functional tests)
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in functional tests)
        consensus.AMKHeight = 10000000;
        consensus.BayfrontHeight = 10000000;
        consensus.BayfrontMarinaHeight = 10000000;
        consensus.BayfrontGardensHeight = 10000000;
        consensus.ClarkeQuayHeight = 10000000;
        consensus.DakotaHeight = 10000000;
        consensus.DakotaCrescentHeight = 10000000;
        consensus.EunosHeight = 10000000;
        consensus.EunosKampungHeight = 10000000;
        consensus.EunosPayaHeight = 10000000;
        consensus.FortCanningHeight = 10000000;

        consensus.pos.diffLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.pos.nTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.pos.nTargetTimespanV2 = 14 * 24 * 60 * 60; // two weeks
        consensus.pos.nTargetSpacing = 10 * 60; // 10 minutes
        consensus.pos.nStakeMinAge = 0;
        consensus.pos.nStakeMaxAge = 14 * 24 * 60 * 60; // Two weeks
        consensus.pos.fAllowMinDifficultyBlocks = true; // only for regtest
        consensus.pos.fNoRetargeting = true; // only for regtest

        consensus.pos.allowMintingWithoutPeers = true; // don't mint if no peers connected

        consensus.CSVHeight = 432; // CSV activated on regtest (Used in rpc activation tests)
        consensus.SegwitHeight = 0; // SEGWIT is always activated on regtest unless overridden
        consensus.nRuleChangeActivationThreshold = 108; // 75% for testchains
        consensus.nMinerConfirmationWindow = 144; // Faster than normal for regtest (144 instead of 2016)
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 0;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = Consensus::BIP9Deployment::NO_TIMEOUT;

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00");

        // Masternodes' params
        consensus.mn.activationDelay = 10;
        consensus.mn.newActivationDelay = 20;
        consensus.mn.resignDelay = 10;
        consensus.mn.newResignDelay = 2 * consensus.mn.newActivationDelay;
        consensus.mn.creationFee = 1 * COIN;
        consensus.mn.collateralAmount = 10 * COIN;
        consensus.mn.collateralAmountDakota = 2 * COIN;
        consensus.mn.anchoringTeamSize = 3;
        consensus.mn.anchoringFrequency = 15;

        consensus.mn.anchoringTimeDepth = 3 * 60 * 60;
        consensus.mn.anchoringAdditionalTimeDepth = 15 * 60; // 15 minutes
        consensus.mn.anchoringTeamChange = 15; // Number of blocks

        consensus.token.creationFee = 1 * COIN;
        consensus.token.collateralAmount = 10 * COIN;

        consensus.spv.creationFee = 1000; // should be > bitcoin's dust
        consensus.spv.wallet_xpub = "tpubDA2Mn6LMJ35tYaA1Noxirw2WDzmgKEDKLRbSs2nwF8TTsm2iB6hBJmNjAAEbDqYzZLdThLykWDcytGzKDrjUzR9ZxdmSbFz7rt18vFRYjt9";
        consensus.spv.anchors_address = "n1h1kShnyiw3qRR6MM1FnwShaNVoVwBTnF";
        consensus.spv.anchorSubsidy = 0 * COIN;
        consensus.spv.subsidyIncreasePeriod = 60;
        consensus.spv.subsidyIncreaseValue = 5 * COIN;
        consensus.spv.minConfirmations = 6;

        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::IncentiveFunding, 10 * COIN / 50); // normalized to (COIN == 100%) // 10 per block
        consensus.nonUtxoBlockSubsidies.emplace(CommunityAccountType::AnchorReward, COIN/10 / 50);       // 0.1 per block

        // New coinbase reward distribution
        consensus.dist.masternode = 3333; // 33.33%
        consensus.dist.community = 491; // 4.91%
        consensus.dist.anchor = 2; // 0.02%
        consensus.dist.liquidity = 2545; // 25.45%
        consensus.dist.swap = 1234; // 12.34%
        consensus.dist.futures = 1234; // 12.34%
        consensus.dist.options = 988; // 9.88%
        consensus.dist.unallocated = 173; // 1.73%

        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::AnchorReward, consensus.dist.anchor);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::IncentiveFunding, consensus.dist.liquidity);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Swap, consensus.dist.swap);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Futures, consensus.dist.futures);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Options, consensus.dist.options);
        consensus.newNonUTXOSubsidies.emplace(CommunityAccountType::Unallocated, consensus.dist.unallocated);

        pchMessageStartPostAMK[0] = pchMessageStart[0] = 0xfa;
        pchMessageStartPostAMK[1] = pchMessageStart[1] = 0xbf;
        pchMessageStartPostAMK[2] = pchMessageStart[2] = 0xb5;
        pchMessageStartPostAMK[3] = pchMessageStart[3] = 0xda;
        nDefaultPort = 19555;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        UpdateActivationParametersFromArgs(args);

        base58Prefixes[PUBKEY_ADDRESS] = {0x6f};
        base58Prefixes[SCRIPT_ADDRESS] = {0xc4};
        base58Prefixes[SECRET_KEY] =     {0xef};
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "bcrt";

        // (!) after prefixes set
        consensus.foundationShareScript = GetScriptForDestination(DecodeDestination("2NCWAKfEehP3qibkLKYQjXaWMK23k4EDMVS", *this)); // cMv1JaaZ9Mbb3M3oNmcFvko8p7EcHJ8XD7RCQjzNaMs7BWRVZTyR
        consensus.foundationShare = 0; // old style - just percents // stil zero here to not broke old tests
        consensus.foundationShareDFIP1 = 19 * COIN / 10 / 50; // 1.9 DFI @ 50 per block (rate normalized to (COIN == 100%)

        // now it is for devnet and regtest only, 2 first and 2 last of genesis MNs acts as foundation members
        consensus.foundationMembers.emplace(GetScriptForDestination(DecodeDestination("mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU", *this)));
        consensus.foundationMembers.emplace(GetScriptForDestination(DecodeDestination("msER9bmJjyEemRpQoS8YYVL21VyZZrSgQ7", *this)));
        consensus.foundationMembers.emplace(GetScriptForDestination(DecodeDestination("bcrt1qyrfrpadwgw7p5eh3e9h3jmu4kwlz4prx73cqny", *this)));
        consensus.foundationMembers.emplace(GetScriptForDestination(DecodeDestination("bcrt1qyeuu9rvq8a67j86pzvh5897afdmdjpyankp4mu", *this)));

        consensus.accountDestruction.clear();
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("2MxJf6Ak8MGrLoGdekrU6AusW29szZUFphH", *this)));
        consensus.accountDestruction.insert(GetScriptForDestination(DecodeDestination("mxiaFfAnCoXEUy4RW8NgsQM7yU5YRCiFSh", *this)));

        // owner base58, operator base58
        vMasternodes.push_back({"mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU", "mswsMVsyGMj1FzDMbbxw2QW3KvQAv2FKiy"});
        vMasternodes.push_back({"msER9bmJjyEemRpQoS8YYVL21VyZZrSgQ7", "mps7BdmwEF2vQ9DREDyNPibqsuSRZ8LuwQ"});
        vMasternodes.push_back({"myF3aHuxtEuqqTw44EurtVs6mjyc1QnGUS", "mtbWisYQmw9wcaecvmExeuixG7rYGqKEU4"});
        vMasternodes.push_back({"mwyaBGGE7ka58F7aavH5hjMVdJENP9ZEVz", "n1n6Z5Zdoku4oUnrXeQ2feLz3t7jmVLG9t"});
        vMasternodes.push_back({"mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu", "mzqdipBJcKX9rXXxcxw2kTHC3Xjzd3siKg"});
        vMasternodes.push_back({"mud4VMfbBqXNpbt8ur33KHKx8pk3npSq8c", "mk5DkY4qcV6CUpuxDVyD3AHzRq5XK9kbRN"});
        vMasternodes.push_back({"bcrt1qyrfrpadwgw7p5eh3e9h3jmu4kwlz4prx73cqny", "bcrt1qmfvw3dp3u6fdvqkdc0y3lr0e596le9cf22vtsv"});
        vMasternodes.push_back({"bcrt1qyeuu9rvq8a67j86pzvh5897afdmdjpyankp4mu", "bcrt1qurwyhta75n2g75u2u5nds9p6w9v62y8wr40d2r"});

        // For testing send after Eunos: 93ViFmLeJVgKSPxWGQHmSdT5RbeGDtGW4bsiwQM2qnQyucChMqQ
        consensus.burnAddress = GetScriptForDestination(DecodeDestination("mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG", *this));
        consensus.retiredBurnAddress = GetScriptForDestination(DecodeDestination("mfdefichainDSTBurnAddressXXXZcE1vs", *this));

        if (isJellyfish) {
            std::vector<CTxOut> initdist;
            // first 2 owner & first 2 operator get 100 mill DFI
            initdist.push_back(CTxOut(100000000 * COIN, GetScriptForDestination(DecodeDestination("mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU", *this))));
            initdist.push_back(CTxOut(100000000 * COIN, GetScriptForDestination(DecodeDestination("mswsMVsyGMj1FzDMbbxw2QW3KvQAv2FKiy", *this))));
            initdist.push_back(CTxOut(100000000 * COIN, GetScriptForDestination(DecodeDestination("msER9bmJjyEemRpQoS8YYVL21VyZZrSgQ7", *this))));
            initdist.push_back(CTxOut(100000000 * COIN, GetScriptForDestination(DecodeDestination("mps7BdmwEF2vQ9DREDyNPibqsuSRZ8LuwQ", *this))));
            initdist.push_back(CTxOut(consensus.baseBlockSubsidy, GetScriptForDestination(DecodeDestination("mud4VMfbBqXNpbt8ur33KHKx8pk3npSq8c", *this))));

            // 6th masternode owner. for initdist tests
            genesis = CreateGenesisBlock(1579045065, 0x207fffff, 1, initdist,CreateGenesisMasternodes()); // old=1296688602
            consensus.hashGenesisBlock = genesis.GetHash();

            assert(consensus.hashGenesisBlock == uint256S("0xd744db74fb70ed42767ae028a129365fb4d7de54ba1b6575fb047490554f8a7b"));
            assert(genesis.hashMerkleRoot == uint256S("0x5615dbbb379da893dd694e02d25a7955e1b7471db55f42bbd82b5d3f5bdb8d38"));
        }
        else {
            genesis = CreateGenesisBlock(1579045065, 0x207fffff, 1, {
                                          CTxOut(consensus.baseBlockSubsidy,
                                          GetScriptForDestination(DecodeDestination("mud4VMfbBqXNpbt8ur33KHKx8pk3npSq8c", *this)) // 6th masternode owner. for initdist tests
                                          )},
                                      CreateGenesisMasternodes()); // old=1296688602
            consensus.hashGenesisBlock = genesis.GetHash();

            assert(consensus.hashGenesisBlock == uint256S("0x0091f00915b263d08eba2091ba70ba40cea75242b3f51ea29f4a1b8d7814cd01"));
            assert(genesis.hashMerkleRoot == uint256S("0xc4b6f1f9a7bbb61121b949b57be05e8651e7a0c55c38eb8aaa6c6602b1abc444"));
        }

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = true;
        m_is_test_chain = true;

        checkpointData = {
            {
                {0, consensus.hashGenesisBlock},
            }
        };

        chainTxData = ChainTxData{
            0,
            0,
            0
        };
    }

    /**
     * Allows modifying the Version Bits regtest parameters.
     */
    void UpdateVersionBitsParameters(Consensus::DeploymentPos d, int64_t nStartTime, int64_t nTimeout)
    {
        consensus.vDeployments[d].nStartTime = nStartTime;
        consensus.vDeployments[d].nTimeout = nTimeout;
    }
    void UpdateActivationParametersFromArgs(const ArgsManager& args);
};

/// Check for fork height based flag, validate and set the value to a target var
boost::optional<int> UpdateHeightValidation(const std::string& argName, const std::string& argFlag, int& argTarget) {
    if (gArgs.IsArgSet(argFlag)) {
        int64_t height = gArgs.GetArg(argFlag, argTarget);
        if (height < -1 || height >= std::numeric_limits<int>::max()) {
            auto lowerArgName = boost::to_lower_copy(argName);
            throw std::runtime_error(strprintf(
                "Activation height %ld for %s is out of valid range. Use -1 to disable %s.",
                height, argName, lowerArgName));
        } else if (height == -1) {
            LogPrintf("%s disabled for testing\n", argName);
            height = std::numeric_limits<int>::max();
        }
        argTarget = static_cast<int>(height);
        return height;
    }
    return {};
}

void CRegTestParams::UpdateActivationParametersFromArgs(const ArgsManager& args)
{
    UpdateHeightValidation("Segwit", "-segwitheight", consensus.SegwitHeight);
    UpdateHeightValidation("AMK", "-amkheight", consensus.AMKHeight);
    UpdateHeightValidation("Bayfront", "-bayfrontheight", consensus.BayfrontHeight);
    UpdateHeightValidation("Bayfront Gardens", "-bayfrontgardensheight", consensus.BayfrontGardensHeight);
    UpdateHeightValidation("Clarke Quay", "-clarkequayheight", consensus.ClarkeQuayHeight);
    UpdateHeightValidation("Dakota", "-dakotaheight", consensus.DakotaHeight);
    UpdateHeightValidation("Dakota Crescent", "-dakotacrescentheight", consensus.DakotaCrescentHeight);
    auto eunosHeight = UpdateHeightValidation("Eunos", "-eunosheight", consensus.EunosHeight);
    if (eunosHeight.has_value()){
        consensus.EunosKampungHeight = static_cast<int>(eunosHeight.get());
    }
    UpdateHeightValidation("Eunos Paya", "-eunospayaheight", consensus.EunosPayaHeight);

    if (gArgs.IsArgSet("-fortcanningheight")) {
        int64_t height = gArgs.GetArg("-fortcanningheight", consensus.FortCanningHeight);
        if (height < -1 || height >= std::numeric_limits<int>::max()) {
            throw std::runtime_error(strprintf("Activation height %ld for Fort Canning is out of valid range. Use -1 to disable Fort Canning features.", height));
        } else if (height == -1) {
            LogPrintf("Fort Canning disabled for testing\n");
            height = std::numeric_limits<int>::max();
        }
        consensus.FortCanningHeight = static_cast<int>(height);
    }

    if (!args.IsArgSet("-vbparams")) return;

    for (const std::string& strDeployment : args.GetArgs("-vbparams")) {
        std::vector<std::string> vDeploymentParams;
        boost::split(vDeploymentParams, strDeployment, boost::is_any_of(":"));
        if (vDeploymentParams.size() != 3) {
            throw std::runtime_error("Version bits parameters malformed, expecting deployment:start:end");
        }
        int64_t nStartTime, nTimeout;
        if (!ParseInt64(vDeploymentParams[1], &nStartTime)) {
            throw std::runtime_error(strprintf("Invalid nStartTime (%s)", vDeploymentParams[1]));
        }
        if (!ParseInt64(vDeploymentParams[2], &nTimeout)) {
            throw std::runtime_error(strprintf("Invalid nTimeout (%s)", vDeploymentParams[2]));
        }
        bool found = false;
        for (int j=0; j < (int)Consensus::MAX_VERSION_BITS_DEPLOYMENTS; ++j) {
            if (vDeploymentParams[0] == VersionBitsDeploymentInfo[j].name) {
                UpdateVersionBitsParameters(Consensus::DeploymentPos(j), nStartTime, nTimeout);
                found = true;
                LogPrintf("Setting version bits activation parameters for %s to start=%ld, timeout=%ld\n", vDeploymentParams[0], nStartTime, nTimeout);
                break;
            }
        }
        if (!found) {
            throw std::runtime_error(strprintf("Invalid deployment (%s)", vDeploymentParams[0]));
        }
    }
}

static std::unique_ptr<const CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

std::unique_ptr<const CChainParams> CreateChainParams(const std::string& chain)
{
    if (chain == CBaseChainParams::MAIN)
        return std::unique_ptr<CChainParams>(new CMainParams());
    else if (chain == CBaseChainParams::TESTNET)
        return std::unique_ptr<CChainParams>(new CTestNetParams());
    else if (chain == CBaseChainParams::DEVNET)
        return std::unique_ptr<CChainParams>(new CDevNetParams());
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams(gArgs));
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}
