#include <rpc/server.h>
#include <rpc/client.h>
#include <rpc/util.h>

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
    pcustomcsview->ForEachToken([&counter] (DCT_ID const & id, CTokenImplementation const & token) {
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
        boost::this_thread::interruption_point();

        result.emplace(it->Key(),it->Value());
    }
    return result;
}

static std::vector<unsigned char> ToBytes(const char * in)
{
    if (!in) return {};
    return std::vector<unsigned char>(in, in + strlen(in) + 1);
}

BOOST_AUTO_TEST_CASE(undo)
{
    CStorageKV & base_raw = pcustomcsview->GetRaw();
    // place some "old" record
    pcustomcsview->Write("testkey1", "value0");

    auto snapStart = TakeSnapshot(base_raw);

    CCustomCSView mnview(*pcustomcsview);
    BOOST_CHECK(mnview.Write("testkey1", "value1")); // modify
    BOOST_CHECK(mnview.Write("testkey2", "value2")); // insert

    // construct undo
    auto& flushable = dynamic_cast<CFlushableStorageKV&>(mnview.GetRaw());
    auto undo = CUndo::Construct(base_raw, flushable.GetRaw());
    BOOST_CHECK(undo.before.size() == 2);
    BOOST_CHECK(undo.before.at(ToBytes("testkey1")) == ToBytes("value0"));
    BOOST_CHECK(undo.before.at(ToBytes("testkey2")).is_initialized() == false);

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


BOOST_AUTO_TEST_SUITE_END()
