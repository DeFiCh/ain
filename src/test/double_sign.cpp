#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <masternodes/masternodes.h>
#include <miner.h>
#include <pos.h>
#include <pos_kernel.h>
#include <validation.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>

static const std::vector<unsigned char> V_OP_TRUE{OP_TRUE};

struct RegtestingSetup : public TestingSetup {
    RegtestingSetup() : TestingSetup(CBaseChainParams::REGTEST) {}
};
BOOST_FIXTURE_TEST_SUITE(double_sign, RegtestingSetup)

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

    do {
        time++;
        pblock->nTime = time;
    } while (!pos::CheckKernelHash(pblock->stakeModifier, pblock->nBits,  (int64_t) pblock->nTime, Params().GetConsensus(), masternodeID).hashOk);

    BOOST_CHECK(!pos::SignPosBlock(pblock, minterKey));

    return pblock;
}

std::vector<CBlockHeader> GenerateTwoCriminalsHeaders(CKey const &minterKey, uint64_t const &mintedBlocks, uint256 const &masternodeID) {
    uint64_t time = Params().GenesisBlock().nTime;
    uint64_t height = 1;
    uint256 prevStakeModifier = Params().GenesisBlock().stakeModifier;

    std::shared_ptr<CBlock> blockOne = FinalizeBlock(
            Block(Params().GenesisBlock().GetHash(), height, mintedBlocks),
            masternodeID,
            minterKey,
            prevStakeModifier);
    std::shared_ptr<CBlock> blockTwo = FinalizeBlock(
            Block(Params().GenesisBlock().GetHash(), height + 1, mintedBlocks),
            masternodeID,
            minterKey,
            prevStakeModifier);

    return {blockOne->GetBlockHeader(), blockTwo->GetBlockHeader()};
}

BOOST_AUTO_TEST_CASE(check_doublesign)
{
    uint256 masternodeID = testMasternodeKeys.begin()->first;
    std::map<uint256, TestMasternodeKeys>::const_iterator pos = testMasternodeKeys.find(masternodeID);
    BOOST_CHECK(pos != testMasternodeKeys.end());
    CKey minterKey = pos->second.operatorKey;
    uint64_t mintedBlocks = 0;
    std::vector<CBlockHeader> criminalsBlockHeaders = GenerateTwoCriminalsHeaders(minterKey, mintedBlocks, masternodeID);

    pmasternodesview->WriteMintedBlockHeader(masternodeID, mintedBlocks, criminalsBlockHeaders[0].GetHash(), criminalsBlockHeaders[0], false);
    pmasternodesview->WriteMintedBlockHeader(masternodeID, mintedBlocks, criminalsBlockHeaders[1].GetHash(), criminalsBlockHeaders[1], false);
    BOOST_CHECK(!pmasternodesview->CheckDoubleSign(criminalsBlockHeaders[0], criminalsBlockHeaders[1]));

    std::map<uint256, CBlockHeader> blockHeaders;
    BOOST_CHECK(pmasternodesview->FindMintedBlockHeader(masternodeID, mintedBlocks, blockHeaders, false));
    BOOST_CHECK(blockHeaders.size() == 2);
}

BOOST_AUTO_TEST_CASE(check_criminal_entities)
{
    uint256 masternodeID = testMasternodeKeys.begin()->first;
    std::map<uint256, TestMasternodeKeys>::const_iterator pos = testMasternodeKeys.find(masternodeID);
    BOOST_CHECK(pos != testMasternodeKeys.end());
    CKey minterKey = pos->second.operatorKey;
    uint64_t mintedBlocks = 0;
    std::vector<CBlockHeader> criminalsBlockHeaders = GenerateTwoCriminalsHeaders(minterKey, mintedBlocks, masternodeID);

    CValidationState state;

    BOOST_CHECK(ProcessNewBlockHeaders(criminalsBlockHeaders, state, Params()));
    CMasternodesView::CMnCriminals criminals = pmasternodesview->GetUncaughtCriminals();
    BOOST_CHECK(criminals.size() == 1);
    BOOST_CHECK(criminals.begin()->first == masternodeID);
    BOOST_CHECK(criminals[masternodeID].blockHeader.GetHash() == criminalsBlockHeaders[0].GetHash() ||
                criminals[masternodeID].blockHeader.GetHash() == criminalsBlockHeaders[1].GetHash());
    BOOST_CHECK(criminals[masternodeID].conflictBlockHeader.GetHash() == criminalsBlockHeaders[0].GetHash() ||
                criminals[masternodeID].conflictBlockHeader.GetHash() == criminalsBlockHeaders[1].GetHash());
    BOOST_CHECK(criminals[masternodeID].conflictBlockHeader.GetHash() != criminals[masternodeID].blockHeader.GetHash());
}

BOOST_AUTO_TEST_CASE(check_blocking_criminal_coins)
{
    uint256 masternodeID = testMasternodeKeys.begin()->first;
    std::map<uint256, TestMasternodeKeys>::const_iterator pos = testMasternodeKeys.find(masternodeID);
    BOOST_CHECK(pos != testMasternodeKeys.end());
    CKey minterKey = pos->second.operatorKey;
    uint64_t mintedBlocks = 0;
    std::vector<CBlockHeader> criminalsBlockHeaders = GenerateTwoCriminalsHeaders(minterKey, mintedBlocks, masternodeID);

    CValidationState state;

    BOOST_CHECK(ProcessNewBlockHeaders(criminalsBlockHeaders, state, Params()));

    CScript p2pk = CScript() << ToByteVector(minterKey.GetPubKey()) << OP_CHECKSIG;
    CScript scriptSig = GetScriptForWitness(p2pk);
    CScript scriptPubKey = GetScriptForDestination(ScriptHash(scriptSig));

    std::shared_ptr<CBlock> block = FinalizeBlock(
                                        Block(criminalsBlockHeaders[1].GetHash(), 2, ++mintedBlocks),
                                        masternodeID,
                                        minterKey,
                                        criminalsBlockHeaders[1].stakeModifier);

    BOOST_CHECK(ProcessNewBlockHeaders({block->GetBlockHeader()}, state, Params()));

   // BOOST_CHECK(pmasternodesview->FindBlockedCriminalCoins(masternodeID, 0, false));
}

BOOST_AUTO_TEST_SUITE_END()
