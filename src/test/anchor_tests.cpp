#include <chainparams.h>
#include <masternodes/anchors.h>
#include <masternodes/masternodes.h>
#include <spv/spv_wrapper.h>
#include <validation.h>

#include <test/setup_common.h>

#include <boost/test/unit_test.hpp>


struct SpvTestingSetup : public TestingSetup {
    SpvTestingSetup()
        : TestingSetup(CBaseChainParams::REGTEST)
    {
        spv::pspv = MakeUnique<spv::CFakeSpvWrapper>();
    }
    ~SpvTestingSetup()
    {
        spv::pspv->Disconnect();
        spv::pspv.reset();
    }
};

BOOST_FIXTURE_TEST_SUITE(anchor_tests, SpvTestingSetup)

BOOST_AUTO_TEST_CASE(anchor_order_logic)
{
    CAnchorIndex::AnchorRec anchorOne;
    CAnchorIndex::AnchorRec anchorTwo;

    anchorOne.btcHeight = 100;
    anchorTwo.btcHeight = 200;

    // Lowest Bitcoin height wins
    BOOST_CHECK(spv::PendingOrder(anchorOne, anchorTwo) == true);
    BOOST_CHECK(spv::PendingOrder(anchorTwo, anchorOne) == false);

    anchorOne.btcHeight = anchorTwo.btcHeight;
    anchorOne.anchor.height = 100;
    anchorTwo.anchor.height = 200;

    // Heighest DeFi height wins
    BOOST_CHECK(spv::PendingOrder(anchorOne, anchorTwo) == false);
    BOOST_CHECK(spv::PendingOrder(anchorTwo, anchorOne) == true);
    BOOST_CHECK(BestOfTwo(&anchorOne, &anchorTwo)->anchor.height == 200);
    BOOST_CHECK(BestOfTwo(&anchorTwo, &anchorOne)->anchor.height == 200);

    anchorOne.anchor.height = anchorTwo.anchor.height;
    anchorOne.txHash = uint256S("12ca5ac2b666478bbbdfc0e0b328552a8cd83aa1b3fbb822560ab8cbf72be893");
    anchorTwo.txHash = uint256S("852bb89808af5a5487d4afed23b4ec3c4186ec8101ff9e7c73a038c9a2c436d9");

    // Lowest hash wins
    BOOST_CHECK(spv::PendingOrder(anchorOne, anchorTwo) == true);
    BOOST_CHECK(spv::PendingOrder(anchorTwo, anchorOne) == false);
    BOOST_CHECK(BestOfTwo(&anchorOne, &anchorTwo)->txHash == uint256S("12ca5ac2b666478bbbdfc0e0b328552a8cd83aa1b3fbb822560ab8cbf72be893"));
    BOOST_CHECK(BestOfTwo(&anchorTwo, &anchorOne)->txHash == uint256S("12ca5ac2b666478bbbdfc0e0b328552a8cd83aa1b3fbb822560ab8cbf72be893"));

    // Test new anchor ordering logic with randomised hashes
    anchorOne.anchor.height = 10000000;
    anchorTwo.anchor.height = 10000000;

    anchorOne.txHash = uint256S("12ca5ac2b666478bbbdfc0e0b328552a8cd83aa1b3fbb822560ab8cbf72be893"); // 5cfe6594dad4efe238e5c7903ba5afa4c3f92ee81282a43e7ba5919f4cebd210
    anchorTwo.txHash = uint256S("852bb89808af5a5487d4afed23b4ec3c4186ec8101ff9e7c73a038c9a2c436d9"); // 1af2609c24bcbe59af8ffb921129454e12f7aef07da3c3c0fead97711469045a

    BOOST_CHECK(spv::PendingOrder(anchorOne, anchorTwo) == true);
    BOOST_CHECK(spv::PendingOrder(anchorTwo, anchorOne) == false);
    BOOST_CHECK(BestOfTwo(&anchorOne, &anchorTwo)->txHash == uint256S("12ca5ac2b666478bbbdfc0e0b328552a8cd83aa1b3fbb822560ab8cbf72be893"));
    BOOST_CHECK(BestOfTwo(&anchorTwo, &anchorOne)->txHash == uint256S("12ca5ac2b666478bbbdfc0e0b328552a8cd83aa1b3fbb822560ab8cbf72be893"));

    anchorOne.txHash = uint256S("e48106cf7254b73be5d550f2054495b32c4e98f2c2c251697c267ab0a6cb87cf"); // ff8c5aa31428aa787513d1e3451914ed7f8a1b6174e3a572dc5f2a449201240d
    anchorTwo.txHash = uint256S("a5c974e6eca14593bdfd53eaf49c777e4615342370e79705d96b5afd2a016278"); // a06399a5ed47c65452f71174174e9c2696dd3fae83a9d0e89796e195c01b670d

    BOOST_CHECK(spv::PendingOrder(anchorOne, anchorTwo) == true);
    BOOST_CHECK(spv::PendingOrder(anchorTwo, anchorOne) == false);
    BOOST_CHECK(BestOfTwo(&anchorOne, &anchorTwo)->txHash == uint256S("e48106cf7254b73be5d550f2054495b32c4e98f2c2c251697c267ab0a6cb87cf"));
    BOOST_CHECK(BestOfTwo(&anchorTwo, &anchorOne)->txHash == uint256S("e48106cf7254b73be5d550f2054495b32c4e98f2c2c251697c267ab0a6cb87cf"));

    anchorOne.txHash = uint256S("7398ddf9bdabb2c1271b918d3f516fd4573bbead448b4e8a611b7ffd5451777b"); // 9a15e4a213dcd75e035141012e3636c4a46f15ef2ba13ff52357f6121d46901b
    anchorTwo.txHash = uint256S("b2f2ed1fc0b6192b9398b0aef2e79e57d4a473c3e9b2be45e556f7c85e269cbc"); // 699b691491d27aad44fe58d897af97a0e631e6ad27f83408a0d64d933639fd03

    BOOST_CHECK(spv::PendingOrder(anchorOne, anchorTwo) == false);
    BOOST_CHECK(spv::PendingOrder(anchorTwo, anchorOne) == true);
    BOOST_CHECK(BestOfTwo(&anchorOne, &anchorTwo)->txHash == uint256S("b2f2ed1fc0b6192b9398b0aef2e79e57d4a473c3e9b2be45e556f7c85e269cbc"));
    BOOST_CHECK(BestOfTwo(&anchorTwo, &anchorOne)->txHash == uint256S("b2f2ed1fc0b6192b9398b0aef2e79e57d4a473c3e9b2be45e556f7c85e269cbc"));

    anchorOne.txHash = uint256S("3264bb76dc2cdff731733fa33dd530b0058da45606af9824b49b61e1f5ac9d9d"); // f4b83366e8d5650ec7714962b3c6619d737ae43f8b6641b35b4e39ab9605b88f
    anchorTwo.txHash = uint256S("851d8697118d6688b6552cb142a95f461b45e61b9accafa1ef3386b1be0cc2bb"); // c9f467a9e6233f9614d88111b333336a29be9f6d54ac7171fce0f5f58eed04e9

    BOOST_CHECK(spv::PendingOrder(anchorOne, anchorTwo) == true);
    BOOST_CHECK(spv::PendingOrder(anchorTwo, anchorOne) == false);
    BOOST_CHECK(BestOfTwo(&anchorOne, &anchorTwo)->txHash == uint256S("3264bb76dc2cdff731733fa33dd530b0058da45606af9824b49b61e1f5ac9d9d"));
    BOOST_CHECK(BestOfTwo(&anchorTwo, &anchorOne)->txHash == uint256S("3264bb76dc2cdff731733fa33dd530b0058da45606af9824b49b61e1f5ac9d9d"));

    anchorOne.txHash = uint256S("87c638cfe4efa94d8e259978c55a85de101cafaac68c9f6c03b3dc0335016b55"); // af431c6193434919a996fb87aba8790c65f49116f489a7a72e08af02276804ca
    anchorTwo.txHash = uint256S("390a8b3b581e75e13e8eec4fc7fe0b35a382e9fba29d9b42c547e1b6c6785a51"); // 1ecf49be7a49c081245b6df1ba9f7a0463f7da3a33c505145c963be8f741b086

    BOOST_CHECK(spv::PendingOrder(anchorOne, anchorTwo) == false);
    BOOST_CHECK(spv::PendingOrder(anchorTwo, anchorOne) == true);
    BOOST_CHECK(BestOfTwo(&anchorOne, &anchorTwo)->txHash == uint256S("390a8b3b581e75e13e8eec4fc7fe0b35a382e9fba29d9b42c547e1b6c6785a51"));
    BOOST_CHECK(BestOfTwo(&anchorTwo, &anchorOne)->txHash == uint256S("390a8b3b581e75e13e8eec4fc7fe0b35a382e9fba29d9b42c547e1b6c6785a51"));
}

BOOST_AUTO_TEST_CASE(best_anchor_activation_logic)
{
    spv::CFakeSpvWrapper * fspv = static_cast<spv::CFakeSpvWrapper *>(spv::pspv.get());

    LOCK(cs_main);

    auto top = panchors->GetActiveAnchor();
    BOOST_CHECK(top == nullptr);
    auto team0 = panchors->GetCurrentTeam(panchors->GetActiveAnchor());

    // Stage 1. Same btc height. The very first, no prevs (btc height = 1)
    // create first anchor
    {
        CAnchorAuthMessage auth({uint256(), 15, uint256S("def15"), team0});
        CAnchor anc = CAnchor::Create({ auth }, CTxDestination(PKHash()));
        BOOST_CHECK(panchors->AddAnchor(anc, uint256S("bc1"), 1, false) == true);
        BOOST_CHECK(panchors->AddAnchor(anc, uint256S("bc1"), 1, false) == false); // duplicate
        BOOST_CHECK(panchors->AddAnchor(anc, uint256S("bc1"), 1, true) == true);   // duplicate, overwrite
    }

    // fail to activate - nonconfirmed
    BOOST_CHECK(fspv->GetLastBlockHeight() == 0);
    BOOST_CHECK(panchors->ActivateBestAnchor(true) == false);
    BOOST_CHECK(panchors->GetActiveAnchor() == nullptr);

    fspv->lastBlockHeight = 6; panchors->UpdateLastHeight(fspv->GetLastBlockHeight());

    // confirmed, active
    BOOST_CHECK(panchors->ActivateBestAnchor(true) == true);
    top = panchors->GetActiveAnchor();
    BOOST_REQUIRE(top != nullptr);
    BOOST_CHECK(top->btcHeight == 1);
    BOOST_CHECK(top->txHash == uint256S("bc1"));
    BOOST_CHECK(top->anchor.height == 15);
    BOOST_CHECK(top->anchor.previousAnchor == uint256());

    // add at the same btc height, with worse tx hash, but with higher defi height - should be choosen
    {
        CAnchorAuthMessage auth({uint256(), 30, uint256S("def30a"), team0});
        CAnchor anc = CAnchor::Create({ auth }, CTxDestination(PKHash()));
        BOOST_CHECK(panchors->AddAnchor(anc, uint256S("bd1"), 1) == true);
    }
    BOOST_CHECK(panchors->ActivateBestAnchor(true) == true);
    top = panchors->GetActiveAnchor();
    BOOST_REQUIRE(top != nullptr);
    BOOST_CHECK(top->btcHeight == 1);
    BOOST_CHECK(top->txHash == uint256S("bd1"));
    BOOST_CHECK(top->anchor.height == 30);
    BOOST_CHECK(top->anchor.previousAnchor == uint256());

    // add at the same btc height, with same defi height, but with lower txhash - should be choosen
    {
        CAnchorAuthMessage auth({uint256(), 30, uint256S("def30b"), team0});
        CAnchor anc = CAnchor::Create({ auth }, CTxDestination(PKHash()));
        BOOST_CHECK(panchors->AddAnchor(anc, uint256S("bb1"), 1) == true);
    }
    BOOST_CHECK(panchors->ActivateBestAnchor(true) == true);
    top = panchors->GetActiveAnchor();
    BOOST_REQUIRE(top != nullptr);
    BOOST_CHECK(top->btcHeight == 1);
    BOOST_CHECK(top->txHash == uint256S("bb1"));
    BOOST_CHECK(top->anchor.height == 30);
    BOOST_CHECK(top->anchor.previousAnchor == uint256());

    // add at the same btc height, with same defi height, but with higher (worse) txhash - should stay untouched
    {
        CAnchorAuthMessage auth({uint256(), 30, uint256S("def30c"), team0});
        CAnchor anc = CAnchor::Create({ auth }, CTxDestination(PKHash()));
        BOOST_CHECK(panchors->AddAnchor(anc, uint256S("be1"), 1) == true);
    }
    BOOST_CHECK(panchors->ActivateBestAnchor(true) == false);
    top = panchors->GetActiveAnchor();
    BOOST_REQUIRE(top != nullptr);
    BOOST_CHECK(top->txHash == uint256S("bb1"));

    // decrease btc height, all anchors should be deactivated
    fspv->lastBlockHeight = 0; panchors->UpdateLastHeight(fspv->GetLastBlockHeight());
    BOOST_CHECK(panchors->ActivateBestAnchor(true) == true);
    top = panchors->GetActiveAnchor();
    BOOST_REQUIRE(top == nullptr);

    // revert to prev state, activate again
    fspv->lastBlockHeight = 6; panchors->UpdateLastHeight(fspv->GetLastBlockHeight());
    BOOST_CHECK(panchors->ActivateBestAnchor(true) == true);
    top = panchors->GetActiveAnchor();
    BOOST_REQUIRE(top != nullptr);
    BOOST_CHECK(top->txHash == uint256S("bb1"));


    // Stage 2. Next btc height (btc height = 2)
    // creating anc with old (wrong, empty) prev
    fspv->lastBlockHeight = 12; panchors->UpdateLastHeight(fspv->GetLastBlockHeight());
    {
        CAnchorAuthMessage auth({uint256(), 45, uint256S("def45a"), team0});
        CAnchor anc = CAnchor::Create({ auth }, CTxDestination(PKHash()));
        BOOST_CHECK(panchors->AddAnchor(anc, uint256S("bc2"), 2) == true);
    }
    BOOST_CHECK(panchors->ActivateBestAnchor(true) == false);
    top = panchors->GetActiveAnchor();
    BOOST_REQUIRE(top != nullptr);
    BOOST_CHECK(top->txHash == uint256S("bb1"));

    // create anc with correct prev
    {
        CAnchorAuthMessage auth({top->txHash, 45, uint256S("def45b"), team0});
        CAnchor anc = CAnchor::Create({ auth }, CTxDestination(PKHash()));
        BOOST_CHECK(panchors->AddAnchor(anc, uint256S("bd2"), 2) == true);
    }
    BOOST_CHECK(panchors->ActivateBestAnchor(true) == true);
    top = panchors->GetActiveAnchor();
    BOOST_REQUIRE(top != nullptr);
    BOOST_CHECK(top->btcHeight == 2);
    BOOST_CHECK(top->txHash == uint256S("bd2"));
    BOOST_CHECK(top->anchor.height == 45);
    BOOST_CHECK(top->anchor.previousAnchor == uint256S("bb1"));

    // decrease btc height, fall to prev state (we already did that, but with empty top)
    fspv->lastBlockHeight = 6; panchors->UpdateLastHeight(fspv->GetLastBlockHeight());
    BOOST_CHECK(panchors->ActivateBestAnchor(true) == true);
    top = panchors->GetActiveAnchor();
    BOOST_REQUIRE(top != nullptr);
    BOOST_CHECK(top->txHash == uint256S("bb1"));

    // advance to btc height = 2 again
    fspv->lastBlockHeight = 12; panchors->UpdateLastHeight(fspv->GetLastBlockHeight());
    BOOST_CHECK(panchors->ActivateBestAnchor(true) == true);
    top = panchors->GetActiveAnchor();
    BOOST_REQUIRE(top != nullptr);
    BOOST_CHECK(top->txHash == uint256S("bd2"));

    // and the last - delete (!) parent anc (simulate btc chain reorg, but in more wild way: not the very top block entirely, but one prev tx, bearing tx)
    BOOST_CHECK(panchors->DeleteAnchorByBtcTx(uint256S("bb1")) == true);
    BOOST_CHECK(panchors->ActivateBestAnchor(true) == true);
    top = panchors->GetActiveAnchor();
    BOOST_REQUIRE(top != nullptr);
    BOOST_CHECK(top->txHash == uint256S("bd1"));
}

// This test will check for the correct functionality of function CAnchorIndex::GetLatestAnchorUpToDeFiHeight()
BOOST_AUTO_TEST_CASE(Test_GetLatestAnchorUpToDeFiHeight)
{
    //create valid setup
    spv::CFakeSpvWrapper * fspv = static_cast<spv::CFakeSpvWrapper *>(spv::pspv.get());

    LOCK(cs_main);

    auto top = panchors->GetActiveAnchor();
    BOOST_CHECK(top == nullptr);
    auto team0 = panchors->GetCurrentTeam(panchors->GetActiveAnchor());

    // no anchors yet. call the GetMostLatestAtDeFiHeight() at DeFi height 20
    // should return nullptr
    auto falbackAnchor = panchors->GetLatestAnchorUpToDeFiHeight(20);
    BOOST_CHECK(falbackAnchor == nullptr);

    // add first anchor
    {
        CAnchorAuthMessage auth({uint256(), 15, uint256S("def15"), team0});
        CAnchor anc = CAnchor::Create({ auth }, CTxDestination(PKHash()));
        BOOST_CHECK(panchors->AddAnchor(anc, uint256S("bc1"), 1, false) == true);
    }

    fspv->lastBlockHeight = 6; panchors->UpdateLastHeight(fspv->GetLastBlockHeight());

    //confirm the top
    BOOST_CHECK(panchors->ActivateBestAnchor(true) == true);
    top = panchors->GetActiveAnchor();
    BOOST_REQUIRE(top != nullptr);
    BOOST_CHECK(top->btcHeight == 1);
    BOOST_CHECK(top->txHash == uint256S("bc1"));
    BOOST_CHECK(top->anchor.height == 15);
    BOOST_CHECK(top->anchor.previousAnchor == uint256());

    //call the GetMostLatestAtDeFiHeight() at DeFi height 20
    falbackAnchor = nullptr;
    falbackAnchor = panchors->GetLatestAnchorUpToDeFiHeight(20);

    //check against the top anchor above.
    BOOST_REQUIRE(falbackAnchor != nullptr);
    BOOST_CHECK(falbackAnchor->btcHeight == top->btcHeight);
    BOOST_CHECK(falbackAnchor->txHash == top->txHash);
    BOOST_CHECK(falbackAnchor->anchor.height == top->anchor.height);
    BOOST_CHECK(falbackAnchor->anchor.previousAnchor == top->anchor.previousAnchor);
    BOOST_CHECK(falbackAnchor->anchor.height < 20);

    //add another anchor at DeFi height 30 and btcHeight 2
    {
        CAnchorAuthMessage auth({uint256(), 30, uint256S("def30"), team0});
        CAnchor anc = CAnchor::Create({ auth }, CTxDestination(PKHash()));
        BOOST_CHECK(panchors->AddAnchor(anc, uint256S("bc2"), 2, false) == true);
    }
    
    //call the GetMostLatestAtDeFiHeight() at DeFi height 40
    falbackAnchor = nullptr;
    falbackAnchor = panchors->GetLatestAnchorUpToDeFiHeight(40);

    //check against the last inserted anchor which was at DeFi height 30
    BOOST_REQUIRE(falbackAnchor != nullptr);
    BOOST_CHECK(falbackAnchor->btcHeight == 2);
    BOOST_CHECK(falbackAnchor->txHash == uint256S("bc2"));
    BOOST_CHECK(falbackAnchor->anchor.height == 30);
    BOOST_CHECK(falbackAnchor->anchor.height < 40);

    //add another anchor at DeFi height 45 and btcHeight 2 but different btc hash
    {
        CAnchorAuthMessage auth({uint256(), 45, uint256S("def45"), team0});
        CAnchor anc = CAnchor::Create({ auth }, CTxDestination(PKHash()));
        BOOST_CHECK(panchors->AddAnchor(anc, uint256S("bc3"), 2, false) == true);
    }

    //call the GetMostLatestAtDeFiHeight() at DeFi height 40
    falbackAnchor = nullptr;
    falbackAnchor = panchors->GetLatestAnchorUpToDeFiHeight(40);

    //check against the anchor at DeFi height 30 
    BOOST_REQUIRE(falbackAnchor != nullptr);
    BOOST_CHECK(falbackAnchor->btcHeight == 2);
    BOOST_CHECK(falbackAnchor->txHash == uint256S("bc2"));
    BOOST_CHECK(falbackAnchor->anchor.height == 30);
    BOOST_CHECK(falbackAnchor->anchor.height < 40);

    //add another anchor at DeFi height 45 and btcHeight 2 but different btc hash
    {
        CAnchorAuthMessage auth({uint256(), 45, uint256S("def45"), team0});
        CAnchor anc = CAnchor::Create({ auth }, CTxDestination(PKHash()));
        BOOST_CHECK(panchors->AddAnchor(anc, uint256S("bc4"), 2, false) == true);
    }

    //call the GetMostLatestAtDeFiHeight() at DeFi height 45
    falbackAnchor = nullptr;
    falbackAnchor = panchors->GetLatestAnchorUpToDeFiHeight(45);

    //check against the anchor at DeFi height 30 
    BOOST_REQUIRE(falbackAnchor != nullptr);
    BOOST_CHECK(falbackAnchor->btcHeight == 2);
    BOOST_CHECK(falbackAnchor->txHash == uint256S("bc2"));
    BOOST_CHECK(falbackAnchor->anchor.height == 30);
    BOOST_CHECK(falbackAnchor->anchor.height < 45);
}

BOOST_AUTO_TEST_SUITE_END()
