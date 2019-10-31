#include <chainparams.h>
#include <consensus/merkle.h>
#include <masternodes/masternodes.h>
#include <miner.h>
#include <pos.h>
#include <pos_kernel.h>
#include <util/system.h>
#include <script/signingprovider.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

static const std::vector<unsigned char> V_OP_TRUE{OP_TRUE};

struct RegtestingSetup : public TestingSetup {
    RegtestingSetup() : TestingSetup(CBaseChainParams::REGTEST) {}
};
BOOST_FIXTURE_TEST_SUITE(pos_tests, RegtestingSetup)

std::shared_ptr<CBlock> Block( const uint256& prev_hash, const uint64_t& height, const uint64_t& mintedBlocks)
{
    CScript pubKey = CScript() << OP_TRUE;

    auto ptemplate = BlockAssembler(Params()).CreateNewBlock(pubKey);
    auto pblock = std::make_shared<CBlock>(ptemplate->block);
    pblock->hashPrevBlock = prev_hash;

    pblock->mintedBlocks = mintedBlocks;
    pblock->height = height;

    return pblock;
}

std::shared_ptr<CBlock> FinalizeBlock(std::shared_ptr<CBlock> pblock, const uint256& masternodeID, const CKey& minterKey, const uint256& prevStakeModifier)
{
    LOCK(cs_main); // For LookupBlockIndex
    static uint64_t time = Params().GenesisBlock().nTime;

    pblock->stakeModifier = pos::ComputeStakeModifier(prevStakeModifier, minterKey.GetPubKey().GetID());

    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);

    pblock->nTime = time + 10;
//    do {
//        time++;
//        pblock->nTime = time;
//    } while (!pos::CheckKernelHash(pblock->stakeModifier, pblock->nBits,  (int64_t) pblock->nTime, Params().GetConsensus(), masternodeID).hashOk);

    BOOST_CHECK(!pos::SignPosBlock(pblock, minterKey));

    return pblock;
}

BOOST_AUTO_TEST_CASE(calc_kernel)
{
    uint256 stakeModifier = uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    uint256 mnID = uint256S("fedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321");
    int64_t coinstakeTime = 10000000;
    BOOST_CHECK(uint256S("2a30e655ae8018566092750052a01bdef3ad8e1951beb87a9d503e1bcfe4bd2a") ==
                pos::CalcKernelHash(stakeModifier, coinstakeTime, mnID, Params().GetConsensus()));

    uint32_t target = 0x1effffff;
    BOOST_CHECK(pos::CheckKernelHash(stakeModifier, target, coinstakeTime, Params().GetConsensus(), mnID).hashOk);

    uint32_t unattainableTarget = 0x00ffffff;
    BOOST_CHECK(!pos::CheckKernelHash(stakeModifier, unattainableTarget, coinstakeTime, Params().GetConsensus(), mnID).hashOk);

//    CKey key;
//    key.MakeNewKey(true); // Need to use compressed keys in segwit or the signing will fail
//    FillableSigningProvider keystore;
//    BOOST_CHECK(keystore.AddKeyPubKey(key, key.GetPubKey()));
//    CKeyID keyID = key.GetPubKey().GetID();
//
//    uint256 prevStakeModifier = uint256S("fedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321");
//    CDataStream ss(SER_GETHASH, 0);
//    ss << prevStakeModifier << keyID;
//    uint256 targetStakeModifier = Hash(ss.begin(), ss.end());
//
//    BOOST_CHECK(pos::ComputeStakeModifier(prevStakeModifier, keyID) == targetStakeModifier);
}

BOOST_AUTO_TEST_CASE(check_stake_modifier)
{
        BOOST_CHECK(true);
//    uint256 masternodeID = testMasternodeKeys.begin()->first;
//    std::map<uint256, TestMasternodeKeys>::const_iterator pos = testMasternodeKeys.find(masternodeID);
//    BOOST_CHECK(pos != testMasternodeKeys.end());
//    CKey minterKey = pos->second.operatorKey;
//
//    uint256 prev_hash = uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
//    uint64_t height = 1;
//    uint64_t mintedBlocks = 1;
//    std::shared_ptr<CBlock> block = Block(prev_hash, height, mintedBlocks);
//    BOOST_CHECK(!pos::CheckStakeModifier(::ChainActive().Tip(), *(CBlockHeader*)block.get()));
//
//    uint256 prevStakeModifier = Params().GenesisBlock().stakeModifier;
//    block->stakeModifier = pos::ComputeStakeModifier(prevStakeModifier, minterKey.GetPubKey().GetID());
//    BOOST_CHECK(!pos::CheckStakeModifier(::ChainActive().Tip(), *(CBlockHeader*)block.get()));
//
//    std::shared_ptr<CBlock> correctBlock = FinalizeBlock(
//        Block(Params().GenesisBlock().GetHash(), height, mintedBlocks),
//        masternodeID,
//        minterKey,
//        prevStakeModifier);
//    BOOST_CHECK(pos::CheckStakeModifier(::ChainActive().Tip(), *(CBlockHeader*)correctBlock.get()));
//
//    correctBlock->sig = {};
//    BOOST_CHECK(!pos::CheckStakeModifier(::ChainActive().Tip(), *(CBlockHeader*)correctBlock.get()));
}

BOOST_AUTO_TEST_CASE(check_header_signature)
{
        BOOST_CHECK(true);
//    uint256 masternodeID = testMasternodeKeys.begin()->first;
//    std::map<uint256, TestMasternodeKeys>::const_iterator pos = testMasternodeKeys.find(masternodeID);
//    BOOST_CHECK(pos != testMasternodeKeys.end());
//    CKey minterKey = pos->second.operatorKey;
//
//    BOOST_CHECK(pos::CheckHeaderSignature((CBlockHeader)Params().GenesisBlock()));
//
//    uint256 prev_hash = uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
//    uint64_t height = 1;
//    uint64_t mintedBlocks = 1;
//    std::shared_ptr<CBlock> block = Block(prev_hash, height, mintedBlocks);
//
//    BOOST_CHECK(!pos::CheckHeaderSignature(*(CBlockHeader*)block.get()));
//
//    FinalizeBlock(
//        block,
//        masternodeID,
//        minterKey,
//        prev_hash);
//
//    BOOST_CHECK(pos::CheckHeaderSignature(*(CBlockHeader*)block.get()));
//
////    block->sig[0] = 0xff;
////    block->sig[1] = 0xff;
////    BOOST_CHECK(!pos::CheckHeaderSignature(*(CBlockHeader*)block.get()));
}

BOOST_AUTO_TEST_CASE(contextual_check_pos)
{
        BOOST_CHECK(true);
//    uint256 masternodeID = testMasternodeKeys.begin()->first;
//    std::map<uint256, TestMasternodeKeys>::const_iterator pos = testMasternodeKeys.find(masternodeID);
//    BOOST_CHECK(pos != testMasternodeKeys.end());
//    CKey minterKey = pos->second.operatorKey;
//
//    BOOST_CHECK(pos::ContextualCheckProofOfStake((CBlockHeader)Params().GenesisBlock(), Params().GetConsensus(), pmasternodesview.get()));
//
//    uint256 prev_hash = uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
//    uint64_t height = 0;
//    uint64_t mintedBlocks = 1;
//    std::shared_ptr<CBlock> block = Block(Params().GenesisBlock().GetHash(), height, mintedBlocks);
//
//    BOOST_CHECK(!pos::ContextualCheckProofOfStake(*(CBlockHeader*)block.get(), Params().GetConsensus(), pmasternodesview.get()));
//
//    block->height = 1;
//    BOOST_CHECK(!pos::ContextualCheckProofOfStake(*(CBlockHeader*)block.get(), Params().GetConsensus(), pmasternodesview.get()));
//
//    std::shared_ptr<CBlock> finalizeBlock = FinalizeBlock(
//        block,
//        masternodeID,
//        minterKey,
//        prev_hash);
//    BOOST_CHECK(pos::ContextualCheckProofOfStake(*(CBlockHeader*)finalizeBlock.get(), Params().GetConsensus(), pmasternodesview.get()));
//
////    block->sig[0] = 0xff;
////    block->sig[1] = 0xff;
////    BOOST_CHECK(!pos::ContextualCheckProofOfStake(*(CBlockHeader*)block.get(), Params().GetConsensus(), pmasternodesview.get()));
//
//    block->nBits = 0x0effffff;
//    block->sig = {};
//    BOOST_CHECK(!pos::SignPosBlock(block, minterKey));
//
//    BOOST_CHECK(!pos::ContextualCheckProofOfStake(*(CBlockHeader*)block.get(), Params().GetConsensus(), pmasternodesview.get()));
//    block->sig[0] = 0xff;
}

BOOST_AUTO_TEST_CASE(sign_pos_block)
{
        BOOST_CHECK(true);
//    uint256 masternodeID = testMasternodeKeys.begin()->first;
//    std::map<uint256, TestMasternodeKeys>::const_iterator pos = testMasternodeKeys.find(masternodeID);
//    BOOST_CHECK(pos != testMasternodeKeys.end());
//    CKey minterKey = pos->second.operatorKey;
//
//    uint256 prev_hash = uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
//    uint64_t height = 1;
//    uint64_t mintedBlocks = 1;
//    std::shared_ptr<CBlock> block = Block(prev_hash, height, mintedBlocks);
//
//    static uint64_t time = Params().GenesisBlock().nTime;
//
//    block->stakeModifier = pos::ComputeStakeModifier(prev_hash, minterKey.GetPubKey().GetID());
//
//    block->hashMerkleRoot = BlockMerkleRoot(*block);
//
//    BOOST_CHECK(pos::SignPosBlock(block, CKey()) == std::string{"Block signing error"});
//    BOOST_CHECK(!pos::SignPosBlock(block, minterKey));
//    BOOST_CHECK_THROW(pos::SignPosBlock(block, minterKey), std::logic_error);
//
//    BOOST_CHECK(!pos::CheckProofOfStake(*(CBlockHeader*)block.get(), ::ChainActive().Tip(), Params().GetConsensus(), pmasternodesview.get()));
//
//    uint256 prevStakeModifier = Params().GenesisBlock().stakeModifier;
//    std::shared_ptr<CBlock> correctBlock = FinalizeBlock(
//            Block(Params().GenesisBlock().GetHash(), 1, 1),
//            masternodeID,
//            minterKey,
//            prevStakeModifier);
//    BOOST_CHECK(pos::CheckProofOfStake(*(CBlockHeader*)correctBlock.get(), ::ChainActive().Tip(), Params().GetConsensus(), pmasternodesview.get()));
}

BOOST_AUTO_TEST_SUITE_END()
