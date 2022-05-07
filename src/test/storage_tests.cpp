#include <rpc/server.h>
#include <rpc/client.h>

#include <interfaces/chain.h>
#include <key_io.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_rpc.h>
#include <rpc/rawtransaction_util.h>
#include <test/setup_common.h>

#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>

#include <univalue.h>

BOOST_FIXTURE_TEST_SUITE(storage_tests, TestingSetup)

class HasReason {
public:
    explicit HasReason(const std::string& reason) : m_reason(reason) {}
    bool operator() (const UniValue& e) const {
        return find_value(e, "message").get_str().find(m_reason) != std::string::npos;
    };
private:
    const std::string m_reason;
};

// Copied from rpc_tests.cpp
UniValue CallRPC(std::string args)
{
    std::vector<std::string> vArgs;
    boost::split(vArgs, args, boost::is_any_of(" \t"));
    std::string strMethod = vArgs[0];
    vArgs.erase(vArgs.begin());
    JSONRPCRequest request;
    request.strMethod = strMethod;
    request.params = RPCConvertValues(strMethod, vArgs);
    request.fHelp = false;
    if (RPCIsInWarmup(nullptr)) SetRPCWarmupFinished();
    try {
        UniValue result = tableRPC.execute(request);
        return result;
    }
    catch (const UniValue& objError) {
        throw std::runtime_error(find_value(objError, "message").get_str());
    }
}

int GetTokensCount(CCustomCSView& view)
{
    int counter{0};
    view.ForEachToken([&counter] (DCT_ID const & id, CLazySerialize<CTokenImplementation>) {
//        printf("DCT_ID: %d, Token: %s: %s\n", id, token.symbol.c_str(), token.name.c_str()); // dump for debug
        ++counter;
        return true;
    });
    return counter;
}

std::map<TBytes, TBytes> TakeSnapshot(CStorageKV const & storage)
{
    std::map<TBytes, TBytes> result;
    TBytes key;
    auto it = storage.NewIterator();
    for(it.Seek(key); it.Valid(); it.Next()) {
        result.emplace(it.Key(), it.Value());
    }
    return result;
}

static std::vector<unsigned char> ToBytes(const char * in)
{
    if (!in) return {};
    return std::vector<unsigned char>(in, in + strlen(in) + 1);
}

BOOST_AUTO_TEST_CASE(flushableType)
{
    auto& mainStorage = pcustomcsview->GetStorage();
    BOOST_REQUIRE(mainStorage.GetStorageLevelDB());

    CCustomCSView mnview(*pcustomcsview);
    auto& cacheStorage = mnview.GetStorage();
    BOOST_REQUIRE(cacheStorage.GetFlushableStorage());
}

BOOST_AUTO_TEST_CASE(undo)
{
    CCustomCSView view(*pcustomcsview);
    CUndosView undoView(*pundosView);
    auto& base_raw = view.GetStorage();
    auto& undo_raw = undoView.GetStorage();
    auto undoStart = TakeSnapshot(undo_raw);

    // place some "old" record
    view.Write("testkey1", "value0");

    auto snapStart = TakeSnapshot(base_raw);

    auto mnview(view);
    BOOST_CHECK(mnview.Write("testkey1", "value1")); // modify
    BOOST_CHECK(mnview.Write("testkey2", "value2")); // insert

    // construct undo
    auto flushable = mnview.GetStorage().GetFlushableStorage();
    auto undo = CUndo::Construct(base_raw, flushable->GetRaw());
    BOOST_CHECK(undo.before.size() == 2);
    BOOST_CHECK(undo.before.at(ToBytes("testkey1")) == ToBytes("value0"));
    BOOST_CHECK(undo.before.at(ToBytes("testkey2")).has_value() == false);

    // flush changes
    mnview.Flush();

    auto snap1 = TakeSnapshot(base_raw);
    BOOST_CHECK(snap1.size() - snapStart.size() == 1); // onew new record
    BOOST_CHECK(snap1.at(ToBytes("testkey1")) == ToBytes("value1"));
    BOOST_CHECK(snap1.at(ToBytes("testkey2")) == ToBytes("value2"));

    // write undo
    auto snap_undo1 = TakeSnapshot(base_raw);
    undoView.SetUndo({1, uint256S("0x1"), UndoSource::CustomView}, undo);

    auto snap_undo = TakeSnapshot(undo_raw);
    BOOST_CHECK_EQUAL(snap_undo.size() - undoStart.size(), 1); // undo

    auto snap2 = TakeSnapshot(base_raw);
    undoView.OnUndoTx(UndoSource::CustomView, mnview, uint256S("0x1"), 2); // fail
    mnview.Flush();
    BOOST_CHECK(snap2 == TakeSnapshot(base_raw));
    undoView.OnUndoTx(UndoSource::CustomView, mnview, uint256S("0x2"), 1); // fail
    mnview.Flush();
    BOOST_CHECK(snap2 == TakeSnapshot(base_raw));
    undoView.OnUndoTx(UndoSource::CustomView, mnview, uint256S("0x1"), 1); // success
    mnview.Flush();
    BOOST_CHECK(snapStart == TakeSnapshot(base_raw));
}

BOOST_AUTO_TEST_CASE(recipients)
{
    auto testChain = interfaces::MakeChain();
    std::string p2pkh = "8Jb2J9BHWNYsMnVNQqvzHf38UuePXvE6Cd";
    auto goodScript = GetScriptForDestination(DecodeDestination(p2pkh));

    // check wrong address/script first
    BOOST_CHECK_EXCEPTION(DecodeRecipients(*testChain, ParseNonRFCJSONValue("{\"\":\"1\"}")), UniValue, HasReason("does not refer to any valid address"));
    BOOST_CHECK_EXCEPTION(DecodeRecipients(*testChain, ParseNonRFCJSONValue("{\"0\":\"1\"}")), UniValue, HasReason("does not refer to any valid address"));
    BOOST_CHECK_EXCEPTION(DecodeRecipients(*testChain, ParseNonRFCJSONValue("{\"abcdef1234567890\":\"1\"}")), UniValue, HasReason("does not solvable"));

    // check wrong tokens
    // BOOST_CHECK_EXCEPTION(DecodeRecipients(*testChain, ParseNonRFCJSONValue("{\"" + p2pkh + "\":\"1@1\"}")), UniValue, HasReason("Invalid Defi token")); // note that if tokenId is digit - existance not checked
    BOOST_CHECK_EXCEPTION(DecodeRecipients(*testChain, ParseNonRFCJSONValue("{\"" + p2pkh + "\":\"1@GOLD\"}")), UniValue, HasReason("Invalid Defi token"));

    // check wrong amounts
    BOOST_CHECK_EXCEPTION(DecodeRecipients(*testChain, ParseNonRFCJSONValue("{\"" + p2pkh + "\":\"non-int\"}")), UniValue, HasReason("Invalid amount"));
    BOOST_CHECK_EXCEPTION(DecodeRecipients(*testChain, ParseNonRFCJSONValue("{\"" + p2pkh + "\":\"0\"}")), UniValue, HasReason("Amount out of range"));
    BOOST_CHECK_EXCEPTION(DecodeRecipients(*testChain, ParseNonRFCJSONValue("{\"" + p2pkh + "\":\"-1\"}")), UniValue, HasReason("Amount out of range"));

    // check good script/address
    {
        auto res = DecodeRecipients(*testChain, ParseNonRFCJSONValue("{\"" + goodScript.GetHex() + "\":\"1\"}"));
        BOOST_REQUIRE(res.size() == 1);
        BOOST_CHECK(res.begin()->first ==  goodScript);

        BOOST_CHECK(DecodeRecipients(*testChain, ParseNonRFCJSONValue("{\"dF4zajDUeVc3BrQiuiL7SRm2XVbAhRDL6c\":\"1\"}")).size() == 1);          // p2sh-segwit
        BOOST_CHECK(DecodeRecipients(*testChain, ParseNonRFCJSONValue("{\"df1q08zhfacgzgzuh0zdtd585hs9rv5rzksz4wrn2z\":\"1\"}")).size() == 1);  // bech32
    }

    // check multiple dests
    {
        BOOST_CHECK_EXCEPTION(DecodeRecipients(*testChain, ParseNonRFCJSONValue("{\"" + p2pkh + "\":\"1\",\"" + p2pkh + "\":\"1\"}")), UniValue, HasReason("duplicate recipient"));

        // good
        auto res = DecodeRecipients(*testChain, ParseNonRFCJSONValue("{\"" + p2pkh + "\":[\"100@1\",\"200@2\"],\"dF4zajDUeVc3BrQiuiL7SRm2XVbAhRDL6c\":\"1@DFI\"}"));
        BOOST_REQUIRE(res.size() == 2);
        BOOST_CHECK(res.at(goodScript).balances ==  (TAmounts{ { DCT_ID{1}, 100*COIN }, { DCT_ID{2}, 200*COIN } }) );
        BOOST_CHECK(res.at(GetScriptForDestination(DecodeDestination("dF4zajDUeVc3BrQiuiL7SRm2XVbAhRDL6c"))).balances ==  (TAmounts{ { DCT_ID{0}, 1*COIN } }) );
    }

}

BOOST_AUTO_TEST_CASE(tokens)
{
    CCustomCSView view(*pcustomcsview);
    BOOST_REQUIRE(GetTokensCount(view) == 1);
    {   // search by id
        auto token = view.GetToken(DCT_ID{0});
        BOOST_REQUIRE(token);
        BOOST_REQUIRE(token->symbol == "DFI");
    }
    {   // search by symbol
        auto pair = view.GetToken("DFI");
        BOOST_REQUIRE(pair);
        BOOST_REQUIRE(pair->first == DCT_ID{0});
        BOOST_REQUIRE(pair->second);
        BOOST_REQUIRE(pair->second->symbol == "DFI");
    }

    // token creation
    CTokenImplementation token1;
    token1.symbol = "DCT1";
    token1.creationTx = uint256S("0x1111");
    BOOST_REQUIRE(view.CreateToken(token1, false).ok);
    BOOST_REQUIRE(GetTokensCount(view) == 2);
    {   // search by id
        auto token = view.GetToken(DCT_ID{128});
        BOOST_REQUIRE(token);
        BOOST_REQUIRE(token->symbol == "DCT1");
    }
    {   // search by symbol
        auto pair = view.GetToken("DCT1#128");
        BOOST_REQUIRE(pair);
        BOOST_REQUIRE(pair->first == DCT_ID{128});
        BOOST_REQUIRE(pair->second);
        BOOST_REQUIRE(pair->second->symbol == "DCT1");
    }
    {   // search by tx
        auto pair = view.GetTokenByCreationTx(uint256S("0x1111"));
        BOOST_REQUIRE(pair);
        BOOST_REQUIRE(pair->first == DCT_ID{128});
        BOOST_REQUIRE(pair->second.creationTx == uint256S("0x1111"));
    }

    // another token creation
    BOOST_REQUIRE(view.CreateToken(token1, false).ok == false); /// duplicate symbol & tx
    token1.symbol = "DCT2";
    BOOST_REQUIRE(view.CreateToken(token1, false).ok == false); /// duplicate tx
    token1.creationTx = uint256S("0x2222");
    BOOST_REQUIRE(view.CreateToken(token1, false).ok);
    BOOST_REQUIRE(GetTokensCount(view) == 3);
    {   // search by id
        auto token = view.GetToken(DCT_ID{129});
        BOOST_REQUIRE(token);
        BOOST_REQUIRE(token->symbol == "DCT2");
    }
    {   // search by symbol
        auto pair = view.GetToken("DCT2#129");
        BOOST_REQUIRE(pair);
        BOOST_REQUIRE(pair->first == DCT_ID{129});
        BOOST_REQUIRE(pair->second);
        BOOST_REQUIRE(pair->second->symbol == "DCT2");
    }
    {   // search by tx
        auto pair = view.GetTokenByCreationTx(uint256S("0x2222"));
        BOOST_REQUIRE(pair);
        BOOST_REQUIRE(pair->first == DCT_ID{129});
        BOOST_REQUIRE(pair->second.creationTx == uint256S("0x2222"));
    }

    {   // search by id
        auto token = view.GetToken(DCT_ID{129});
        BOOST_REQUIRE(token);
        auto tokenImpl = static_cast<CTokenImplementation &>(*token);
        BOOST_REQUIRE(tokenImpl.destructionHeight == -1);
        BOOST_REQUIRE(tokenImpl.destructionTx == uint256{});
    }
    BOOST_REQUIRE(GetTokensCount(view) == 3);
}

struct TestForward {
    uint32_t n;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(WrapBigEndian(n));
    }
    static constexpr unsigned char prefix() { return 'F'; }
};

struct TestBackward {
    uint32_t n;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(n));
            n = ~n;
        }
        else {
            uint32_t neg = ~n;
            READWRITE(WrapBigEndian(neg));
        }
    }
    static constexpr unsigned char prefix() { return 'B'; };
};

BOOST_AUTO_TEST_CASE(ForEachTest)
{
    {
        pcustomcsview->WriteBy<TestForward>(TestForward{0}, 1);
        pcustomcsview->WriteBy<TestForward>(TestForward{1}, 2);
        pcustomcsview->WriteBy<TestForward>(TestForward{255}, 3);
        pcustomcsview->WriteBy<TestForward>(TestForward{256}, 4);
        pcustomcsview->WriteBy<TestForward>(TestForward{((uint16_t)-1) -1}, 5);
        pcustomcsview->WriteBy<TestForward>(TestForward{(uint16_t)-1}, 6);
        pcustomcsview->WriteBy<TestForward>(TestForward{((uint32_t)-1) -1}, 7);
        pcustomcsview->WriteBy<TestForward>(TestForward{((uint32_t)-1)}, 8);
        pcustomcsview->Flush();

        int test = 1;
        pcustomcsview->ForEach<TestForward, TestForward, int>([&] (TestForward const & key, int value) {
            BOOST_CHECK(value == test);
            ++test;
            return true;
        });
    }
    {
        pcustomcsview->WriteBy<TestBackward>(TestBackward{0}, 1);
        pcustomcsview->WriteBy<TestBackward>(TestBackward{1}, 2);
        pcustomcsview->WriteBy<TestBackward>(TestBackward{255}, 3);
        pcustomcsview->WriteBy<TestBackward>(TestBackward{256}, 4);
        pcustomcsview->WriteBy<TestBackward>(TestBackward{((uint16_t)-1) -1}, 5);
        pcustomcsview->WriteBy<TestBackward>(TestBackward{(uint16_t)-1}, 6);
        pcustomcsview->Flush();

        int test = 6;
        pcustomcsview->ForEach<TestBackward, TestBackward, int>([&] (TestBackward const & key, int value) {
            BOOST_CHECK(value == test);
            --test;
            return true;
        }, TestBackward{ (uint32_t) -1 });
    }
}

BOOST_AUTO_TEST_CASE(LowerBoundTest)
{
    {
        CCustomCSView view(*pcustomcsview);
        view.WriteBy<TestForward>(TestForward{0}, 1);
        view.WriteBy<TestForward>(TestForward{1}, 2);
        view.WriteBy<TestForward>(TestForward{255}, 3);
        view.WriteBy<TestForward>(TestForward{256}, 4);
        view.WriteBy<TestForward>(TestForward{((uint16_t)-1) -1}, 5);
        view.WriteBy<TestForward>(TestForward{(uint16_t)-1}, 6);
        view.WriteBy<TestForward>(TestForward{((uint32_t)-1) -1}, 7);
        view.WriteBy<TestForward>(TestForward{((uint32_t)-1)}, 8);

        int test = 4;
        auto it = view.LowerBound<TestForward>(TestForward{256});
        while (it.Valid()) {
            BOOST_CHECK(it.Key().n >= 256);
            BOOST_CHECK(it.Value().as<int>() == test);
            test++;
            it.Next();
        }
        BOOST_CHECK(test == 9);
        // go backward
        test--;
        it.Seek(TestForward{((uint32_t)-1)});
        while (it.Valid()) {
            BOOST_CHECK_EQUAL(it.Value().as<int>(), test);
            test--;
            it.Prev();
        }
        BOOST_CHECK(test == 0);

        CCustomCSView view2(view);
        view2.WriteBy<TestForward>(TestForward{1}, 11);
        view2.WriteBy<TestForward>(TestForward{256}, 9);
        view2.EraseBy<TestForward>(TestForward{255});

        auto flushable2 = view2.GetStorage().GetFlushableStorage();
        {
            // single level iterator over view2 values{11, 9} key 255 does not present
            auto it = NewKVIterator<TestForward>(TestForward{0}, flushable2->GetRaw());
            BOOST_CHECK(it.Valid());
            BOOST_CHECK(it.Value().as<int>() == 11);
            it.Next();
            BOOST_CHECK(it.Value().as<int>() == 9);
            it.Next();
            BOOST_CHECK(!it.Valid());
        }

        {
            auto it = NewKVIterator<TestForward>(TestForward{2}, flushable2->GetRaw());
            BOOST_CHECK(it.Valid());
            BOOST_CHECK(it.Value().as<int>() == 9);
            it.Prev();
            BOOST_CHECK(it.Valid());
            BOOST_CHECK(it.Value().as<int>() == 11);
            it.Prev();
            BOOST_CHECK(!it.Valid());
        }

        CCustomCSView view3(view2);
        view3.EraseBy<TestForward>(TestForward{1});

        {
            auto it = view3.LowerBound<TestForward>(TestForward{256});
            BOOST_CHECK(it.Valid());
            BOOST_CHECK(it.Value().as<int>() == 9);
            it.Prev();
            BOOST_CHECK(it.Valid());
            BOOST_CHECK(it.Value().as<int>() == 1);
            it.Next();
            BOOST_CHECK(it.Valid());
            BOOST_CHECK(it.Value().as<int>() == 9);
            it.Prev();
            BOOST_CHECK(it.Valid());
            BOOST_CHECK(it.Value().as<int>() == 1);
            it.Prev();
            BOOST_CHECK(!it.Valid());
        }

        {
            // view3 has an empty kv storage
            auto flushable3 = view3.GetStorage().GetFlushableStorage();
            auto it = NewKVIterator<TestForward>(TestForward{0}, flushable3->GetRaw());
            BOOST_CHECK(!it.Valid());
        }
    }

    {
        CCustomCSView view(*pcustomcsview);
        view.WriteBy<TestBackward>(TestBackward{0}, 1);
        view.WriteBy<TestBackward>(TestBackward{1}, 2);
        view.WriteBy<TestBackward>(TestBackward{255}, 3);
        view.WriteBy<TestBackward>(TestBackward{256}, 4);

        auto it = view.LowerBound<TestBackward>(TestBackward{254});
        int test = 2;
        // go forward (prev in backward)
        while (it.Valid()) {
            BOOST_CHECK(it.Value().as<int>() == test);
            test++;
            it.Prev();
        }
        BOOST_CHECK(test == 5);

        CCustomCSView view2(view);
        view2.WriteBy<TestBackward>(TestBackward{256}, 5);

        {
            test = 5;
            auto it = view2.LowerBound<TestBackward>(TestBackward{257});
            while (it.Valid()) {
                BOOST_CHECK(it.Value().as<int>() == test);
                test == 5 ? test-=2 : test--;
                it.Next();
            }
            BOOST_CHECK(test == 0);

            it.Seek(TestBackward{254});
            test = 2;
            // go forward (prev in backward)
            while (it.Valid()) {
                BOOST_CHECK(it.Value().as<int>() == test);
                test == 3 ? test+=2 : test++;
                it.Prev();
            }
            BOOST_CHECK(test == 6);

            it.Seek(TestBackward{255});
            BOOST_CHECK(it.Valid());
            it.Prev();
            BOOST_CHECK(it.Valid());
            BOOST_CHECK(it.Value().as<int>() == 5);
            it.Next();
            BOOST_CHECK(it.Valid());
            BOOST_CHECK(it.Value().as<int>() == 3);
            it.Next();
            BOOST_CHECK(it.Valid());
            BOOST_CHECK(it.Value().as<int>() == 2);
        }
    }
}

BOOST_AUTO_TEST_CASE(SnapshotTest)
{
    {
        pcustomcsview->WriteBy<TestForward>(TestForward{0}, 1);
        pcustomcsview->WriteBy<TestForward>(TestForward{1}, 2);
        pcustomcsview->Flush();

        CCustomCSView view1(*pcustomcsview);

        pcustomcsview->WriteBy<TestForward>(TestForward{2}, 5);
        pcustomcsview->WriteBy<TestForward>(TestForward{3}, 6);
        pcustomcsview->Flush();

        CCustomCSView view2(*pcustomcsview);

        pcustomcsview->WriteBy<TestForward>(TestForward{5}, 6);
        pcustomcsview->Flush();

        std::map<int, int> results = {
            {0, 1}, {1, 2},
            {2, 5}, {3, 6},
        };

        uint32_t count = 0;
        view1.ForEach<TestForward, TestForward, int>([&](TestForward key, int value) {
            BOOST_REQUIRE(count < 2);
            BOOST_CHECK_EQUAL(key.n, count);
            BOOST_CHECK_EQUAL(results[count], value);
            ++count;
            return true;
        });
        BOOST_CHECK_EQUAL(count, 2);

        count = 0;
        view2.ForEach<TestForward, TestForward, int>([&](TestForward key, int value) {
            BOOST_REQUIRE(count < 4);
            BOOST_CHECK_EQUAL(key.n, count);
            BOOST_CHECK_EQUAL(results[count], value);
            ++count;
            return true;
        });
        BOOST_CHECK_EQUAL(count, 4);
    }
}

BOOST_AUTO_TEST_CASE(ViewFlush)
{
    {
        CCustomCSView view(*pcustomcsview);

        view.WriteBy<TestForward>(TestForward{0}, 1);
        view.WriteBy<TestForward>(TestForward{1}, 2);
        view.Flush();

        CCustomCSView view2(view);
        view2.WriteBy<TestForward>(TestForward{2}, 3);
        view2.Flush();

        int count = 0;
        // view contains view2 changes, pcustomcsview keeps changes in the batch
        view.ForEach<TestForward, TestForward, int>([&](TestForward key, int value) {
            BOOST_REQUIRE(count < 1);
            BOOST_CHECK_EQUAL(key.n, count + 2);
            BOOST_CHECK_EQUAL(value, count + 3);
            ++count;
            return true;
        });
        BOOST_CHECK_EQUAL(count, 1);

        pcustomcsview->Flush();

        auto readed = pcustomcsview->ReadBy<TestForward, int>(TestForward{0});
        BOOST_REQUIRE(readed);
        BOOST_CHECK_EQUAL(*readed, 1);

        count = 0;
        // pcustomcsview does not contains view2 changes
        pcustomcsview->ForEach<TestForward, TestForward, int>([&](TestForward key, int value) {
            BOOST_REQUIRE(count < 2);
            BOOST_CHECK_EQUAL(key.n, count);
            ++count;
            BOOST_CHECK_EQUAL(value, count);
            return true;
        });
        BOOST_CHECK_EQUAL(count, 2);
    }
}

BOOST_AUTO_TEST_CASE(SnapshotParallel)
{
    {
        pcustomcsview->WriteBy<TestForward>(TestForward{0}, 1);
        pcustomcsview->WriteBy<TestForward>(TestForward{1}, 2);
        pcustomcsview->WriteBy<TestForward>(TestForward{2}, 3);
        pcustomcsview->WriteBy<TestForward>(TestForward{3}, 4);
        pcustomcsview->WriteBy<TestForward>(TestForward{4}, 5);
        pcustomcsview->WriteBy<TestForward>(TestForward{5}, 6);
        pcustomcsview->WriteBy<TestForward>(TestForward{6}, 7);
        pcustomcsview->WriteBy<TestForward>(TestForward{7}, 8);
        pcustomcsview->WriteBy<TestForward>(TestForward{8}, 9);
        pcustomcsview->Flush();

        auto testFunc = [&]() {
            int count = 0;
            pcustomcsview->ForEach<TestForward, TestForward, int>([&](TestForward key, int value) {
                BOOST_REQUIRE(count < 9);
                BOOST_CHECK_EQUAL(key.n, count);
                ++count;
                BOOST_CHECK_EQUAL(value, count);
                return true;
            });
            BOOST_CHECK_EQUAL(count, 9);
        };

        constexpr int num_threads = 64;
        std::vector<std::thread> threads;

        for (int i = 0; i < num_threads; i++)
            threads.emplace_back(testFunc);

        for (int i = 0; i < num_threads; i++)
            threads[i].join();
    }
}

BOOST_AUTO_TEST_CASE(CImmutableType)
{
    CImmutableCSView view(*pcustomcsview);
    CTokenAmount amount{{}, 100000};
    BOOST_REQUIRE(view.AddBalance({}, amount).ok);
    BOOST_CHECK_EQUAL(view.GetBalance({}, DCT_ID{}), amount);
    CCustomCSView& mnview = view;
    BOOST_REQUIRE(!mnview.Flush());
    BOOST_CHECK_EQUAL(pcustomcsview->GetBalance({}, DCT_ID{}), CTokenAmount{});
}

BOOST_AUTO_TEST_SUITE_END()
