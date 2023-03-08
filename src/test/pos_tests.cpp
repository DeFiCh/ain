#include <chainparams.h>
#include <consensus/merkle.h>
#include <masternodes/masternodes.h>
#include <miner.h>
#include <pos.h>
#include <pos_kernel.h>
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
    pblock->deprecatedHeight = height;

    return pblock;
}

std::shared_ptr<CBlock> FinalizeBlock(std::shared_ptr<CBlock> pblock, const uint256& masternodeID, const CKey& minterKey, const uint256& prevStakeModifier)
{
    static uint64_t time = Params().GenesisBlock().nTime;
    pblock->stakeModifier = pos::ComputeStakeModifier(prevStakeModifier, minterKey.GetPubKey().GetID());
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
    pblock->nTime = time + 10;
    BOOST_CHECK(!pos::SignPosBlock(pblock, minterKey));
    return pblock;
}

BOOST_AUTO_TEST_CASE(calc_kernel)
{
    uint256 stakeModifier = uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    uint256 mnID = uint256S("fedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321");
    int64_t coinstakeTime = 10000000;
    BOOST_CHECK(uint256S("2a30e655ae8018566092750052a01bdef3ad8e1951beb87a9d503e1bcfe4bd2a") ==
                pos::CalcKernelHash(stakeModifier, 1, coinstakeTime, mnID));

    uint32_t target = 0x1effffff;
    CheckContextState ctxState;
    BOOST_CHECK(pos::CheckKernelHash(stakeModifier, target, 1, coinstakeTime, 0, mnID, Params().GetConsensus(), {0, 0, 0, 0}, 0, ctxState));

    uint32_t unattainableTarget = 0x00ffffff;
    BOOST_CHECK(!pos::CheckKernelHash(stakeModifier, unattainableTarget, 1, coinstakeTime, 0, mnID, Params().GetConsensus(), {0, 0, 0, 0}, 0, ctxState));

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
    uint256 masternodeID = testMasternodeKeys.begin()->first;
    auto pos = testMasternodeKeys.find(masternodeID);
    BOOST_CHECK(pos != testMasternodeKeys.end());
    CKey minterKey = pos->second.operatorKey;

    uint256 prev_hash = uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    uint64_t height = 1;
    uint64_t mintedBlocks = 1;
    std::shared_ptr<CBlock> block = Block(prev_hash, height, mintedBlocks);
    BOOST_CHECK(!pos::CheckStakeModifier(::ChainActive().Tip(), *(CBlockHeader*)block.get()));

    uint256 prevStakeModifier = Params().GenesisBlock().stakeModifier;
    block->stakeModifier = pos::ComputeStakeModifier(prevStakeModifier, minterKey.GetPubKey().GetID());
    BOOST_CHECK(!pos::CheckStakeModifier(::ChainActive().Tip(), *(CBlockHeader*)block.get()));

    std::shared_ptr<CBlock> correctBlock = FinalizeBlock(
        Block(Params().GenesisBlock().GetHash(), height, mintedBlocks),
        masternodeID,
        minterKey,
        prevStakeModifier);
    BOOST_CHECK(pos::CheckStakeModifier(::ChainActive().Tip(), *(CBlockHeader*)correctBlock.get()));

    correctBlock->SetNull();
    correctBlock->hashPrevBlock = prev_hash;
    BOOST_CHECK(!pos::CheckStakeModifier(::ChainActive().Tip(), *(CBlockHeader*)correctBlock.get()));
}

BOOST_AUTO_TEST_CASE(check_header_signature)
{
    uint256 masternodeID = testMasternodeKeys.begin()->first;
    auto pos = testMasternodeKeys.find(masternodeID);
    BOOST_CHECK(pos != testMasternodeKeys.end());
    CKey minterKey = pos->second.operatorKey;

    BOOST_CHECK(pos::CheckHeaderSignature((CBlockHeader)Params().GenesisBlock()));

    uint256 prev_hash = uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    uint64_t height = 1;
    uint64_t mintedBlocks = 1;
    std::shared_ptr<CBlock> block = Block(prev_hash, height, mintedBlocks);

    BOOST_CHECK(!pos::CheckHeaderSignature(*(CBlockHeader*)block.get()));

    FinalizeBlock(
        block,
        masternodeID,
        minterKey,
        prev_hash);

    BOOST_CHECK(pos::CheckHeaderSignature(*(CBlockHeader*)block.get()));

//    block->sig[0] = 0xff;
//    block->sig[1] = 0xff;
//    BOOST_CHECK(!pos::CheckHeaderSignature(*(CBlockHeader*)block.get()));
}

BOOST_AUTO_TEST_CASE(contextual_check_pos)
{
    uint256 masternodeID = testMasternodeKeys.begin()->first;
    auto pos = testMasternodeKeys.find(masternodeID);
    BOOST_CHECK(pos != testMasternodeKeys.end());
    CKey minterKey = pos->second.operatorKey;
    CheckContextState ctxState;

    BOOST_CHECK(pos::ContextualCheckProofOfStake((CBlockHeader)Params().GenesisBlock(), Params().GetConsensus(), pcustomcsview.get(), ctxState, 0));

//    uint256 prev_hash = uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    uint64_t height = 0;
    uint64_t mintedBlocks = 1;
    std::shared_ptr<CBlock> block = Block(Params().GenesisBlock().GetHash(), height, mintedBlocks);

    BOOST_CHECK(!pos::ContextualCheckProofOfStake(*(CBlockHeader*)block.get(), Params().GetConsensus(), pcustomcsview.get(), ctxState, 0));

    // Failure against a height of 1
    BOOST_CHECK(!pos::ContextualCheckProofOfStake(*(CBlockHeader*)block.get(), Params().GetConsensus(), pcustomcsview.get(), ctxState, 1));
}

BOOST_AUTO_TEST_CASE(sign_pos_block)
{
    uint256 masternodeID = testMasternodeKeys.begin()->first;
    auto pos = testMasternodeKeys.find(masternodeID);
    BOOST_CHECK(pos != testMasternodeKeys.end());
    CKey minterKey = pos->second.operatorKey;

    uint256 prev_hash = uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    uint64_t height = 1;
    uint64_t mintedBlocks = 1;
    std::shared_ptr<CBlock> block = Block(prev_hash, height, mintedBlocks);

    block->stakeModifier = pos::ComputeStakeModifier(prev_hash, minterKey.GetPubKey().GetID());

    block->hashMerkleRoot = BlockMerkleRoot(*block);

    BOOST_CHECK(pos::SignPosBlock(block, CKey()) == std::string{"Block signing error"});
    BOOST_CHECK(!pos::SignPosBlock(block, minterKey));
    BOOST_CHECK_THROW(pos::SignPosBlock(block, minterKey), std::logic_error);

    BOOST_CHECK(!pos::CheckProofOfStake(*(CBlockHeader*)block.get(), ::ChainActive().Tip(), Params().GetConsensus(), pcustomcsview.get()));
}

BOOST_AUTO_TEST_CASE(check_subnode)
{
    const auto stakeModifier = uint256S(std::string(64, '1'));
    const auto masternodeID = stakeModifier;
    uint32_t nBits{486604799};
    int64_t creationHeight{0};
    uint64_t blockHeight{10000000};
    const std::vector<int64_t> subNodesBlockTime{0, 0, 0, 0};
    const uint16_t timelock{520}; // 10 year timelock
    CheckContextState ctxState;

    // Subnode 0
    int64_t coinstakeTime{7};
    BOOST_CHECK(pos::CheckKernelHash(stakeModifier, nBits, creationHeight, coinstakeTime, blockHeight, masternodeID, Params().GetConsensus(), subNodesBlockTime, timelock, ctxState));
    BOOST_CHECK_EQUAL(ctxState.subNode, 0);

    // Subnode 1
    coinstakeTime = 0;
    BOOST_CHECK(pos::CheckKernelHash(stakeModifier, nBits, creationHeight, coinstakeTime, blockHeight, masternodeID, Params().GetConsensus(), subNodesBlockTime, timelock, ctxState));
    BOOST_CHECK_EQUAL(ctxState.subNode, 1);

    // Subnode 2
    coinstakeTime = 23;
    BOOST_CHECK(pos::CheckKernelHash(stakeModifier, nBits, creationHeight, coinstakeTime, blockHeight, masternodeID, Params().GetConsensus(), subNodesBlockTime, timelock, ctxState));
    BOOST_CHECK_EQUAL(ctxState.subNode, 2);

    // Subnode 3
    coinstakeTime = 5;
    BOOST_CHECK(pos::CheckKernelHash(stakeModifier, nBits, creationHeight, coinstakeTime, blockHeight, masternodeID, Params().GetConsensus(), subNodesBlockTime, timelock, ctxState));
    BOOST_CHECK_EQUAL(ctxState.subNode, 3);
}


BOOST_AUTO_TEST_SUITE_END()
