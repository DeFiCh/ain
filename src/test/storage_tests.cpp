#include <rpc/server.h>
#include <rpc/client.h>

#include <interfaces/chain.h>
#include <key_io.h>
#include <masternodes/masternodes.h>
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

int GetTokensCount()
{
    int counter{0};
    pcustomcsview->ForEachToken([&counter] (DCT_ID const & id, CLazySerialize<CTokenImplementation>) {
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
    auto it = const_cast<CStorageKV&>(storage).NewIterator();
    for(it->Seek(key); it->Valid(); it->Next()) {
        result.emplace(it->Key(),it->Value());
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
    CStorageKV & storage = pcustomcsview->GetStorage();
    BOOST_REQUIRE(dynamic_cast<CFlushableStorageKV*>(&storage));
}

BOOST_AUTO_TEST_CASE(undo)
{
    CStorageKV & base_raw = pcustomcsview->GetStorage();
    // place some "old" record
    pcustomcsview->Write("testkey1", "value0");

    auto snapStart = TakeSnapshot(base_raw);

    CCustomCSView mnview(*pcustomcsview);
    BOOST_CHECK(mnview.Write("testkey1", "value1")); // modify
    BOOST_CHECK(mnview.Write("testkey2", "value2")); // insert

    // construct undo
    auto& flushable = mnview.GetStorage();
    auto undo = CUndo::Construct(base_raw, flushable.GetRaw());
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
    pcustomcsview->SetUndo(UndoKey{1, uint256S("0x1")}, undo);

    auto snap2 = TakeSnapshot(base_raw);
    BOOST_CHECK(snap2.size() - snap1.size() == 1); // undo
    BOOST_CHECK(snap2.size() - snapStart.size() == 2); // onew new record + undo

    pcustomcsview->OnUndoTx(uint256S("0x1"), 2); // fail
    BOOST_CHECK(snap2 == TakeSnapshot(base_raw));
    pcustomcsview->OnUndoTx(uint256S("0x2"), 1); // fail
    BOOST_CHECK(snap2 == TakeSnapshot(base_raw));
    pcustomcsview->OnUndoTx(uint256S("0x1"), 1); // success
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
    BOOST_REQUIRE(GetTokensCount() == 1);
    {   // search by id
        auto token = pcustomcsview->GetToken(DCT_ID{0});
        BOOST_REQUIRE(token);
        BOOST_REQUIRE(token->symbol == "DFI");
    }
    {   // search by symbol
        auto pair = pcustomcsview->GetToken("DFI");
        BOOST_REQUIRE(pair);
        BOOST_REQUIRE(pair->first == DCT_ID{0});
        BOOST_REQUIRE(pair->second);
        BOOST_REQUIRE(pair->second->symbol == "DFI");
    }

    // token creation
    CTokenImplementation token1;
    token1.symbol = "DCT1";
    token1.creationTx = uint256S("0x1111");
    BOOST_REQUIRE(pcustomcsview->CreateToken(token1, false).ok);
    BOOST_REQUIRE(GetTokensCount() == 2);
    {   // search by id
        auto token = pcustomcsview->GetToken(DCT_ID{128});
        BOOST_REQUIRE(token);
        BOOST_REQUIRE(token->symbol == "DCT1");
    }
    {   // search by symbol
        auto pair = pcustomcsview->GetToken("DCT1#128");
        BOOST_REQUIRE(pair);
        BOOST_REQUIRE(pair->first == DCT_ID{128});
        BOOST_REQUIRE(pair->second);
        BOOST_REQUIRE(pair->second->symbol == "DCT1");
    }
    {   // search by tx
        auto pair = pcustomcsview->GetTokenByCreationTx(uint256S("0x1111"));
        BOOST_REQUIRE(pair);
        BOOST_REQUIRE(pair->first == DCT_ID{128});
        BOOST_REQUIRE(pair->second.creationTx == uint256S("0x1111"));
    }

    // another token creation
    BOOST_REQUIRE(pcustomcsview->CreateToken(token1, false).ok == false); /// duplicate symbol & tx
    token1.symbol = "DCT2";
    BOOST_REQUIRE(pcustomcsview->CreateToken(token1, false).ok == false); /// duplicate tx
    token1.creationTx = uint256S("0x2222");
    BOOST_REQUIRE(pcustomcsview->CreateToken(token1, false).ok);
    BOOST_REQUIRE(GetTokensCount() == 3);
    {   // search by id
        auto token = pcustomcsview->GetToken(DCT_ID{129});
        BOOST_REQUIRE(token);
        BOOST_REQUIRE(token->symbol == "DCT2");
    }
    {   // search by symbol
        auto pair = pcustomcsview->GetToken("DCT2#129");
        BOOST_REQUIRE(pair);
        BOOST_REQUIRE(pair->first == DCT_ID{129});
        BOOST_REQUIRE(pair->second);
        BOOST_REQUIRE(pair->second->symbol == "DCT2");
    }
    {   // search by tx
        auto pair = pcustomcsview->GetTokenByCreationTx(uint256S("0x2222"));
        BOOST_REQUIRE(pair);
        BOOST_REQUIRE(pair->first == DCT_ID{129});
        BOOST_REQUIRE(pair->second.creationTx == uint256S("0x2222"));
    }

    // revert create token
    BOOST_REQUIRE(pcustomcsview->RevertCreateToken(uint256S("0xffff")) == false);
    BOOST_REQUIRE(pcustomcsview->RevertCreateToken(uint256S("0x1111")) == false);
    BOOST_REQUIRE(pcustomcsview->RevertCreateToken(uint256S("0x2222")));
    BOOST_REQUIRE(GetTokensCount() == 2);
    {   // search by id
        auto token = pcustomcsview->GetToken(DCT_ID{128});
        BOOST_REQUIRE(token);
        BOOST_REQUIRE(token->symbol == "DCT1");
    }

    // create again, with same tx and dctid
    token1.symbol = "DCT3";
    token1.creationTx = uint256S("0x2222"); // SAME!
    BOOST_REQUIRE(pcustomcsview->CreateToken(token1, false).ok);
    BOOST_REQUIRE(GetTokensCount() == 3);
    {   // search by id
        auto token = pcustomcsview->GetToken(DCT_ID{129});
        BOOST_REQUIRE(token);
        BOOST_REQUIRE(token->symbol == "DCT3");
    }

    {   // search by id
        auto token = pcustomcsview->GetToken(DCT_ID{129});
        BOOST_REQUIRE(token);
        auto tokenImpl = static_cast<CTokenImplementation &>(*token);
        BOOST_REQUIRE(tokenImpl.destructionHeight == -1);
        BOOST_REQUIRE(tokenImpl.destructionTx == uint256{});
    }
    BOOST_REQUIRE(GetTokensCount() == 3);
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

        // single level iterator over view2 values{11, 9} key 255 does not present
        it = NewKVIterator<TestForward>(TestForward{0}, view2.GetStorage().GetRaw());
        BOOST_CHECK(it.Valid());
        BOOST_CHECK(it.Value().as<int>() == 11);
        it.Next();
        BOOST_CHECK(it.Value().as<int>() == 9);
        it.Next();
        BOOST_CHECK(!it.Valid());

        it = NewKVIterator<TestForward>(TestForward{2}, view2.GetStorage().GetRaw());
        BOOST_CHECK(it.Valid());
        BOOST_CHECK(it.Value().as<int>() == 9);
        it.Prev();
        BOOST_CHECK(it.Valid());
        BOOST_CHECK(it.Value().as<int>() == 11);
        it.Prev();
        BOOST_CHECK(!it.Valid());

        CCustomCSView view3(view2);
        view3.EraseBy<TestForward>(TestForward{1});

        it = view3.LowerBound<TestForward>(TestForward{256});
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

        // view3 has an empty kv storage
        it = NewKVIterator<TestForward>(TestForward{0}, view3.GetStorage().GetRaw());
        BOOST_CHECK(!it.Valid());
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

        test = 5;
        it = view2.LowerBound<TestBackward>(TestBackward{257});
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

BOOST_AUTO_TEST_SUITE_END()
