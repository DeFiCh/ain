// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <chainparamsseeds.h>
#include <consensus/merkle.h>
#include <masternodes/masternodes.h>
#include <streams.h>
#include <tinyformat.h>
#include <util/system.h>
#include <util/strencodings.h>
#include <versionbitsinfo.h>

#include <assert.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>


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
        assert(operatorDest.which() == 1 || operatorDest.which() == 4);
        CTxDestination ownerDest = DecodeDestination(addrs.ownerAddress, *this);
        assert(ownerDest.which() == 1 || ownerDest.which() == 4);

        CKeyID operatorAuthKey = operatorDest.which() == 1 ? CKeyID(*boost::get<PKHash>(&operatorDest)) : CKeyID(*boost::get<WitnessV0KeyHash>(&operatorDest)) ;
        genesisTeam.insert(operatorAuthKey);
        CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
        metadata << static_cast<unsigned char>(MasternodesTxType::CreateMasternode)
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
    const char* pszTimestamp = "The Times 03/Jan/2009 Chancellor on brink of second bailout for banks";
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
        consensus.BIP16Exception = uint256(); //("0x00000000000002dc756eebf4f49723ed8d30cc28a5f108eb94b1ba88ac4f9c22");
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 0; // 000000000000000004c2b624ed5d7756c508d90fd0da2c7c679febfa6c4735f0
        consensus.BIP66Height = 0; // 00000000000000000379eaa19dce8c9b722d46ae6a57c2f1a988119488b50931

        consensus.pos.diffLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
//        consensus.pos.nTargetTimespan = 14 * 24 * 60 * 60; // two weeks
//        consensus.pos.nTargetSpacing = 10 * 60; // 10 minutes
        consensus.pos.nTargetTimespan = 5 * 60; // 5 min == 10 blocks
        consensus.pos.nTargetSpacing = 30; // seconds
        consensus.pos.fAllowMinDifficultyBlocks = false; // only for regtest
        consensus.pos.fNoRetargeting = false; // only for regtest

        consensus.pos.coinstakeMaturity = 100;

        consensus.pos.allowMintingWithoutPeers = false; // don't mint if no peers connected

        consensus.CSVHeight = 1; // 000000000000000004a1b34462cb8aeebd5799177f7a29cf28f2d1961716b5b5
        consensus.SegwitHeight = 0; // 0000000000000000001c8018d9cb3b742ef25114f27563e3fc4a1902167f9893
        consensus.nRuleChangeActivationThreshold = 9; //1916; // 95% of 2016
        consensus.nMinerConfirmationWindow = 10; //2016; // nTargetTimespan / nTargetSpacing
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].bit = 28;
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nStartTime = 1199145601; // January 1, 2008
        consensus.vDeployments[Consensus::DEPLOYMENT_TESTDUMMY].nTimeout = 1230767999; // December 31, 2008

        // The best chain should have at least this much work.
        consensus.nMinimumChainWork = uint256S("0x00");

        // By default assume that the signatures in ancestors of this block are valid.
        consensus.defaultAssumeValid = uint256S("0x00"); //("0x0000000000000000000f1c54590ee18d15ec70e68c8cd4cfbadb1b4f11697eee"); //563378

        // Masternodes' params
        consensus.mn.activationDelay = 10;
        consensus.mn.resignDelay = 60;
        consensus.mn.creationFee = 10 * COIN;
        consensus.mn.collateralAmount = 1000000 * COIN;
        consensus.mn.historyFrame = 300;
        consensus.mn.anchoringTeamSize = 5;
        consensus.mn.anchoringFrequency = 15;
        consensus.mn.anchoringLag = 15;

        consensus.spv.creationFee = 100000; // should be > bitcoin's dust
        consensus.spv.anchorSubsidy = 0 * COIN;
        consensus.spv.subsidyIncreasePeriod = 60;
        consensus.spv.subsidyIncreaseValue = 5 * COIN;
        consensus.spv.wallet_xpub = "xpub68vVWYqkpwYT8ZxBhN2buFMTPNFzrJQV19QZmhuwQqKQZHxcXVg36GZCrwPhb7KPpivsGXxvd7g82sJXYnKNqi2ZuHJvhqcwF418YEfGMrv";
        consensus.spv.anchors_address = "1FtZwEZKknoquUb6DyQHFZ6g6oomXJYEcb";
        consensus.spv.minConfirmations = 6;

        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 32-bit integer with any alignment.
         */
        pchMessageStart[0] = 0xf9;
        pchMessageStart[1] = 0xbe;
        pchMessageStart[2] = 0xb4;
        pchMessageStart[3] = 0xd9;
        nDefaultPort = 8555;
        nPruneAfterHeight = 100000;
        m_assumed_blockchain_size = 240;
        m_assumed_chain_state_size = 3;

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,18); // '8' (0('1') for bitcoin)
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,90); // 'd' (5('3') for bitcoin)
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,128); // (128 ('5', 'K' or 'L') for bitcoin)
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x88, 0xB2, 0x1E};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x88, 0xAD, 0xE4};

        bech32_hrp = "df";

        // (!) after prefixes set
        consensus.foundationAddress = DecodeDestination("dZcHjYhKtEM88TtZLjp314H2xZjkztXtRc", *this);
        consensus.foundationShare = 10;

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

        genesis = CreateGenesisBlock(1587883831, 0x1d00ffff, 1, initdist, CreateGenesisMasternodes()); // old=1231006505
        consensus.hashGenesisBlock = genesis.GetHash();
        assert(consensus.hashGenesisBlock == uint256S("0x00000f692f0f43845a0befda67f614d891c4b75bce4ff1965b5a269440c6443e"));
        assert(genesis.hashMerkleRoot == uint256S("0x4161a4faafedd4580ac8a0fdd681358dc21f91d24513bc15b35717eb8f3fb0ea"));

        // Note that of those which support the service bits prefix, most only support a subset of
        // possible options.
        // This is fine at runtime as we'll fall back to using them as a oneshot if they don't support the
        // service bits we want, but we should get them updated to support all service bits wanted by any
        // release ASAP to avoid it where possible.
//        vSeeds.emplace_back("seed.bitcoin.sipa.be"); // Pieter Wuille, only supports x1, x5, x9, and xd
//        vSeeds.emplace_back("dnsseed.bluematt.me"); // Matt Corallo, only supports x9
//        vSeeds.emplace_back("dnsseed.bitcoin.dashjr.org"); // Luke Dashjr
//        vSeeds.emplace_back("seed.bitcoinstats.com"); // Christian Decker, supports x1 - xf
//        vSeeds.emplace_back("seed.bitcoin.jonasschnelli.ch"); // Jonas Schnelli, only supports x1, x5, x9, and xd
//        vSeeds.emplace_back("seed.btc.petertodd.org"); // Peter Todd, only supports x1, x5, x9, and xd
//        vSeeds.emplace_back("seed.bitcoin.sprovoost.nl"); // Sjors Provoost
//        vSeeds.emplace_back("dnsseed.emzy.de"); // Stephan Oeste

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_main, pnSeed6_main + ARRAYLEN(pnSeed6_main));

        fDefaultConsistencyChecks = false;
        fRequireStandard = true;
        m_is_test_chain = false;

        checkpointData = {
            {
//                { 11111, uint256S("0x0000000069e244f73d78e8fd29ba2fd2ed618bd6fa2ee92559f542fdb26e7c1d")},
//                { 33333, uint256S("0x000000002dd5588a74784eaa7ab0507a18ad16a236e7b1ce69f00d7ddfb5d0a6")},
//                { 74000, uint256S("0x0000000000573993a3c9e41ce34471c079dcf5f52a0e824a81e7f953b8661a20")},
//                {105000, uint256S("0x00000000000291ce28027faea320c8d2b054b2e0fe44a773f3eefb151d6bdc97")},
//                {134444, uint256S("0x00000000000005b12ffd4cd315cd34ffd4a594f430ac814c91184a0d42d2b0fe")},
//                {168000, uint256S("0x000000000000099e61ea72015e79632f216fe6cb33d7899acb35b75c8303b763")},
//                {193000, uint256S("0x000000000000059f452a5f7340de6682a977387c17010ff6e6c3bd83ca8b1317")},
//                {210000, uint256S("0x000000000000048b95347e83192f69cf0366076336c639f9b7228e9ba171342e")},
//                {216116, uint256S("0x00000000000001b4f4b433e81ee46494af945cf96014816a4e2370f11b23df4e")},
//                {225430, uint256S("0x00000000000001c108384350f74090433e7fcf79a606b8e797f065b130575932")},
//                {250000, uint256S("0x000000000000003887df1f29024b06fc2200b55f8af8f35453d7be294df2d214")},
//                {279000, uint256S("0x0000000000000001ae8c72a0b0c301f67e3afca10e819efa9041e458e9bd7e40")},
//                {295000, uint256S("0x00000000000000004d9b4ef50f0f9d686fd69db2e03af35a100370c64632a983")},
            }
        };

        chainTxData = ChainTxData{
            // Data from rpc: getchaintxstats 4096 0000000000000000000f1c54590ee18d15ec70e68c8cd4cfbadb1b4f11697eee
            /* nTime    */ 1569396815,
            /* nTxCount */ 0,
            /* dTxRate  */ 0
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
        consensus.BIP16Exception = uint256(); //("0x00000000dd30457c001f4095d208cc1296b0eed002427aa599874af7a432b105");
        consensus.BIP34Height = 0;
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 0; // 00000000007f6655f22f98e72ed80d8b06dc761d5da09df0fa1dc4be4f861eb6
        consensus.BIP66Height = 0; // 000000002104c8c45e99a8853285a3b592602a3ccde2b832481da85e9e4ba182

        consensus.pos.diffLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
//        consensus.pos.nTargetTimespan = 14 * 24 * 60 * 60; // two weeks
//        consensus.pos.nTargetSpacing = 10 * 60; // 10 minutes
        consensus.pos.nTargetTimespan = 5 * 60; // 5 min == 10 blocks
        consensus.pos.nTargetSpacing = 30;
        consensus.pos.fAllowMinDifficultyBlocks = false;
        consensus.pos.fNoRetargeting = false; // only for regtest

        consensus.pos.coinstakeMaturity = 100;

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
        consensus.mn.resignDelay = 60;
        consensus.mn.creationFee = 10 * COIN;
        consensus.mn.collateralAmount = 1000000 * COIN;
        consensus.mn.historyFrame = 300;
        consensus.mn.anchoringTeamSize = 5;
        consensus.mn.anchoringFrequency = 15;
        consensus.mn.anchoringLag = 15;

        consensus.spv.creationFee = 100000; // should be > bitcoin's dust
        consensus.spv.wallet_xpub = "tpubD9RkyYW1ixvD9vXVpYB1ka8rPZJaEQoKraYN7YnxbBxxsRYEMZgRTDRGEo1MzQd7r5KWxH8eRaQDVDaDuT4GnWgGd17xbk6An6JMdN4dwsY";
        consensus.spv.anchors_address = "mpAkq2LyaUvKrJm2agbswrkn3QG9febnqL";
        consensus.spv.anchorSubsidy = 0 * COIN;
        consensus.spv.subsidyIncreasePeriod = 60;
        consensus.spv.subsidyIncreaseValue = 5 * COIN;
        consensus.spv.minConfirmations = 1;

        pchMessageStart[0] = 0x0b;
        pchMessageStart[1] = 0x11;
        pchMessageStart[2] = 0x09;
        pchMessageStart[3] = 0x07;
        nDefaultPort = 18555;
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
        consensus.foundationAddress = DecodeDestination("774LCfQ7TAypxBcoTqhPtrnixL53bFK1Gt", *this);
        consensus.foundationShare = 10;

        // owner base58, operator base58
        vMasternodes.push_back({"7BUYP9XmFdLFUBNE1BCXgjaTFA2jpA27Bs", "7Jxwv6zHx4GtaCsG2B1zDgRBKFGf5Mpq3G"});
        vMasternodes.push_back({"75T71nz565BBFFuVnV7rq2ghKzGcTpGn11", "7QAwDD4AVFvY5eSUo6ky9Dyab1yTf4uKHd"});
        vMasternodes.push_back({"7GCfWCWzFnuNxt3tzLUUWCMitE5N4V26e2", "77FTh5SKpG9xuMsUWJe4wZexyCVdE65viD"});
        vMasternodes.push_back({"7GRrtG5fwuuRpDLMatR3mn9dVtJYkuQLSR", "76VXuzfFx3Ta84rFeLRKDNADskoHnyrrRS"});

        std::vector<CTxOut> initdist;
        initdist.push_back(CTxOut(100000000 * COIN, GetScriptForDestination(DecodeDestination("tjJFYoSHDYtysGhr2WVYB7rMt1s6Nm5Mtz", *this))));
        initdist.push_back(CTxOut(100000000 * COIN, GetScriptForDestination(DecodeDestination("tYXt1fN5kwGhHdRfTMJRHX8VDpYNjZpyCX", *this))));
        initdist.push_back(CTxOut(100000000 * COIN, GetScriptForDestination(DecodeDestination("tYyFtqW3KfZr7RewseEayH7zxuguZTR7jN", *this))));

        genesis = CreateGenesisBlock(1586099762, 0x1d00ffff, 1, initdist, CreateGenesisMasternodes()); // old=1296688602
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("0x000004a689359fa744d93819c762387a88b1483d6e8ffa40ac307d85e390f66a"));
        assert(genesis.hashMerkleRoot == uint256S("0xd22a4361f3b77240fa4b8d3cb32eab9a1ff2076b34961c2540bfff4a4d5ac77d"));

        vFixedSeeds.clear();
        vSeeds.clear();
        // nodes with support for servicebits filtering should be at the top
//        vSeeds.emplace_back("testnet-seed.bitcoin.jonasschnelli.ch");
//        vSeeds.emplace_back("seed.tbtc.petertodd.org");
//        vSeeds.emplace_back("seed.testnet.bitcoin.sprovoost.nl");
//        vSeeds.emplace_back("testnet-seed.bluematt.me"); // Just a static list of stable node(s), only supports x9

        vFixedSeeds = std::vector<SeedSpec6>(pnSeed6_test, pnSeed6_test + ARRAYLEN(pnSeed6_test));

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
        consensus.nSubsidyHalvingInterval = 150;
        consensus.baseBlockSubsidy = 50 * COIN;
        consensus.BIP16Exception = uint256();
        consensus.BIP34Height = 500; // BIP34 activated on regtest (Used in functional tests)
        consensus.BIP34Hash = uint256();
        consensus.BIP65Height = 1351; // BIP65 activated on regtest (Used in functional tests)
        consensus.BIP66Height = 1251; // BIP66 activated on regtest (Used in functional tests)
        consensus.pos.diffLimit = uint256S("00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        consensus.pos.nTargetTimespan = 14 * 24 * 60 * 60; // two weeks
        consensus.pos.nTargetSpacing = 10 * 60; // 10 minutes
        consensus.pos.fAllowMinDifficultyBlocks = true; // only for regtest
        consensus.pos.fNoRetargeting = true; // only for regtest

        consensus.pos.coinstakeMaturity = 100;

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
        consensus.mn.resignDelay = 10;
        consensus.mn.creationFee = 1 * COIN;
        consensus.mn.collateralAmount = 10 * COIN;
        consensus.mn.historyFrame = 300;
        consensus.mn.anchoringTeamSize = 8;
        consensus.mn.anchoringFrequency = 15;
        consensus.mn.anchoringLag = 15;

        consensus.spv.creationFee = 1000; // should be > bitcoin's dust
        consensus.spv.wallet_xpub = "tpubDA2Mn6LMJ35tYaA1Noxirw2WDzmgKEDKLRbSs2nwF8TTsm2iB6hBJmNjAAEbDqYzZLdThLykWDcytGzKDrjUzR9ZxdmSbFz7rt18vFRYjt9";
        consensus.spv.anchors_address = "n1h1kShnyiw3qRR6MM1FnwShaNVoVwBTnF";
        consensus.spv.anchorSubsidy = 0 * COIN;
        consensus.spv.subsidyIncreasePeriod = 60;
        consensus.spv.subsidyIncreaseValue = 5 * COIN;
        consensus.spv.minConfirmations = 1;

        pchMessageStart[0] = 0xfa;
        pchMessageStart[1] = 0xbf;
        pchMessageStart[2] = 0xb5;
        pchMessageStart[3] = 0xda;
        nDefaultPort = 19555;
        nPruneAfterHeight = 1000;
        m_assumed_blockchain_size = 0;
        m_assumed_chain_state_size = 0;

        UpdateActivationParametersFromArgs(args);

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1,111);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1,196);
        base58Prefixes[SECRET_KEY] =     std::vector<unsigned char>(1,239);
        base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0x35, 0x87, 0xCF};
        base58Prefixes[EXT_SECRET_KEY] = {0x04, 0x35, 0x83, 0x94};

        bech32_hrp = "bcrt";

        // (!) after prefixes set
        consensus.foundationAddress = CNoDestination();
        consensus.foundationShare = 0;

        // owner base58, operator base58
        vMasternodes.push_back({"mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU", "mswsMVsyGMj1FzDMbbxw2QW3KvQAv2FKiy"});
        vMasternodes.push_back({"msER9bmJjyEemRpQoS8YYVL21VyZZrSgQ7", "mps7BdmwEF2vQ9DREDyNPibqsuSRZ8LuwQ"});
        vMasternodes.push_back({"myF3aHuxtEuqqTw44EurtVs6mjyc1QnGUS", "mtbWisYQmw9wcaecvmExeuixG7rYGqKEU4"});
        vMasternodes.push_back({"mwyaBGGE7ka58F7aavH5hjMVdJENP9ZEVz", "n1n6Z5Zdoku4oUnrXeQ2feLz3t7jmVLG9t"});
        vMasternodes.push_back({"mgsE1SqrcfUhvuYuRjqy6rQCKmcCVKNhMu", "mzqdipBJcKX9rXXxcxw2kTHC3Xjzd3siKg"});
        vMasternodes.push_back({"mud4VMfbBqXNpbt8ur33KHKx8pk3npSq8c", "mk5DkY4qcV6CUpuxDVyD3AHzRq5XK9kbRN"});
        vMasternodes.push_back({"bcrt1qyrfrpadwgw7p5eh3e9h3jmu4kwlz4prx73cqny", "bcrt1qmfvw3dp3u6fdvqkdc0y3lr0e596le9cf22vtsv"});
        vMasternodes.push_back({"bcrt1qyeuu9rvq8a67j86pzvh5897afdmdjpyankp4mu", "bcrt1qurwyhta75n2g75u2u5nds9p6w9v62y8wr40d2r"});

        genesis = CreateGenesisBlock(1579045065, 0x207fffff, 1, {
                                         CTxOut(consensus.baseBlockSubsidy,
                                         GetScriptForDestination(DecodeDestination("mud4VMfbBqXNpbt8ur33KHKx8pk3npSq8c", *this)) // 6th masternode owner. for initdist tests
                                         )},
                                     CreateGenesisMasternodes()); // old=1296688602
        consensus.hashGenesisBlock = genesis.GetHash();

        assert(consensus.hashGenesisBlock == uint256S("0x000008b0c5a88f9b840aef6199356ecf374628ef872c2399f92c3e3fbf14a506"));
        assert(genesis.hashMerkleRoot == uint256S("0x412e4c7460648699000454f6deb8fac2c8b8ba5f2cf385b0d9d019a82041815a"));

        vFixedSeeds.clear(); //!< Regtest mode doesn't have any fixed seeds.
        vSeeds.clear();      //!< Regtest mode doesn't have any DNS seeds.

        fDefaultConsistencyChecks = true;
        fRequireStandard = true;
        m_is_test_chain = true;

        checkpointData = {
            {
                {0, uint256S("0x000006772a3244d0a5a4911a5cb1d7e910e175f4e4b77c755018459122fa7a89")},
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

void CRegTestParams::UpdateActivationParametersFromArgs(const ArgsManager& args)
{
    if (gArgs.IsArgSet("-segwitheight")) {
        int64_t height = gArgs.GetArg("-segwitheight", consensus.SegwitHeight);
        if (height < -1 || height >= std::numeric_limits<int>::max()) {
            throw std::runtime_error(strprintf("Activation height %ld for segwit is out of valid range. Use -1 to disable segwit.", height));
        } else if (height == -1) {
            LogPrintf("Segwit disabled for testing\n");
            height = std::numeric_limits<int>::max();
        }
        consensus.SegwitHeight = static_cast<int>(height);
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
    else if (chain == CBaseChainParams::REGTEST)
        return std::unique_ptr<CChainParams>(new CRegTestParams(gArgs));
    throw std::runtime_error(strprintf("%s: Unknown chain %s.", __func__, chain));
}

void SelectParams(const std::string& network)
{
    SelectBaseParams(network);
    globalChainParams = CreateChainParams(network);
}
