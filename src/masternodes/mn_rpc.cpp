#include "masternodes.h"

#include <core_io.h>
//#include "../init.h"       // pwalletMain
#include <consensus/validation.h>
#include <key_io.h>
#include <net.h>
#include <rpc/client.h>
#include <rpc/server.h>
#include <rpc/protocol.h>
#include <rpc/util.h>
#include <script/script_error.h>
#include <script/sign.h>
#include <univalue/include/univalue.h>
#include <util/validation.h>
#include <validation.h>
#include <version.h>

//#ifdef ENABLE_WALLET
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>
//#endif

#include <future>
#include <stdexcept>

#include <boost/algorithm/string.hpp>
#include <boost/assign/list_of.hpp>

extern UniValue createrawtransaction(UniValue const & params, bool fHelp); // in rawtransaction.cpp
extern UniValue fundrawtransaction(UniValue const & params, bool fHelp); // in rpcwallet.cpp
extern UniValue signrawtransaction(UniValue const & params, bool fHelp); // in rawtransaction.cpp
extern UniValue sendrawtransaction(UniValue const & params, bool fHelp); // in rawtransaction.cpp
extern UniValue getnewaddress(UniValue const & params, bool fHelp); // in rpcwallet.cpp
extern bool EnsureWalletIsAvailable(bool avoidException); // in rpcwallet.cpp
extern bool DecodeHexTx(CTransaction & tx, std::string const & strHexTx); // in core_io.h
//extern std::string EncodeHexTx(CTransaction const & tx);

extern void ScriptPubKeyToJSON(CScript const & scriptPubKey, UniValue & out, bool fIncludeHex); // in rawtransaction.cpp

namespace {
// stolen from rpc_tests.cpp, can't import cause it's in tests module (not linked in main binary)
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

    // nothing to "try/catch" here, will be passed over
    UniValue result = tableRPC.execute(request);
    return result;
}

} // namespace

void EnsureBlocksDownloaded()
{
//    if (::ChainstateActive().IsInitialBlockDownload() && Params().NetworkIDString() != "regtest")
//    {
//        throw JSONRPCError(RPC_IN_WARMUP, "Try it later. It is initial block download!");
//    }
}

extern void FundTransaction(CWallet* const pwallet, CMutableTransaction& tx, CAmount& fee_out, int& change_position, UniValue options);

static UniValue fundsignsend(CMutableTransaction & mtx, JSONRPCRequest const & request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, false)) {
        return NullUniValue;
    }

    CAmount fee;
    int change_position;
    UniValue options(UniValue::VOBJ);
    options.pushKV("changePosition", mtx.vout.size());
    FundTransaction(pwallet, mtx, fee, change_position, options);

    JSONRPCRequest new_request;
    new_request.id = request.id;
    new_request.URI = request.URI;

    new_request.params.setArray();
    new_request.params.push_back(EncodeHexTx(CTransaction(mtx)));
    UniValue txSigned = signrawtransactionwithwallet(new_request);

    // from "sendrawtransaction"
    {
        CMutableTransaction mtx;
        if (!DecodeHexTx(mtx, txSigned["hex"].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        CTransactionRef tx(MakeTransactionRef(std::move(mtx)));

        CAmount max_raw_tx_fee = {COIN / 10}; /// @todo @maxb check it with 0

        std::string err_string;
        AssertLockNotHeld(cs_main);
        const TransactionError err = BroadcastTransaction(tx, err_string, max_raw_tx_fee, /*relay*/ true, /*wait_callback*/ false);
        if (TransactionError::OK != err) {
            throw JSONRPCTransactionError(err, err_string);
        }
        return tx->GetHash().GetHex();
    }
}


CAmount EstimateMnCreationFee()
{
    // Current height + (1 day blocks) to avoid rejection;
    int targetHeight = ::ChainActive().Height() + 1 + (60 * 60 / Params().GetConsensus().nPowTargetSpacing);
//    size_t targetMnCount = pmasternodesview->GetActiveMasternodes().size() < 4 ? 0 : pmasternodesview->GetActiveMasternodes().size() - 4;
    return GetMnCreationFee(targetHeight);
}

//UniValue mn_estimatemncreationfee(UniValue const & params, bool fHelp)
//{
//    if (fHelp || params.size() != 0)
//        throw std::runtime_error(
//            "mn_estimateannouncementfee\n"
//            "\nEstimates the approximate masternode announcement fee\n"
//            "\nResult:\n"
//            "n :    (numeric) estimated fee\n"
//            "\n"
//            "\nExample:\n"
//            + HelpExampleCli("mn_estimatemncreationfee", ""));

//    LOCK(cs_main);
//    return ValueFromAmount(EstimateMnCreationFee());
//}

/*
 *
 *  Issued by: any
*/
UniValue mn_create(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    RPCHelpMan{"mn_create",
        "\nCreates (and submits to local node and network) a masternode creation transaction with given metadata, spending the given inputs..\n"
        "The first optional argument (may be empty array) is an array of specific UTXOs to spend." +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
            {"prevouts", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "A json array of json objects. Provide it if you want to spent specific UTXOs",
                {
                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                        },
                    },
                },
            },
            {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                {
                    {"operatorAuthAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Masternode operator auth address (P2PKH only, unique)" },
                    {"collateralAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Any valid address for keeping collateral amount (any P2PKH or P2WKH address) - used as owner key"},
                },
            },
        },
        RPCResult{
            "\"hex\"                  (string) The hex-encoded raw transaction with signature(s)\n"
        },
        RPCExamples{
            HelpExampleCli("mn_create", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\" "
                                            "\"{\\\"operatorAuthAddress\\\":\\\"address\\\","
                                               "\\\"collateralAddress\\\":\\\"address\\\""
                                            "}\"")
            + HelpExampleRpc("mn_create", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\" "
                                          "\"{\\\"operatorAuthAddress\\\":\\\"address\\\","
                                             "\\\"collateralAddress\\\":\\\"address\\\""
                                            "}\"")
        },
    }.Check(request);



    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create Masternode while still in Initial Block Download");
    }

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    auto locked_chain = pwallet->chain().lock(); /// @todo @maxb is it enough? (i mean cs_main)
    LOCK(pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    RPCTypeCheck(request.params, { UniValue::VARR, UniValue::VOBJ }, true);
    if (request.params[0].isNull() || request.params[1].isNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 and 2 must be non-null, and argument 2 expected as object with "
                                                  "{\"name\",\"ownerAuthAddress\",\"operatorAuthAddress\",\"ownerRewardAddress\",\"collateralAddress\"}");
    }
    UniValue metaObj = request.params[1].get_obj();
    RPCTypeCheckObj(metaObj, {
                        { "operatorAuthAddress", UniValue::VSTR },
                        { "collateralAddress", UniValue::VSTR }
                    },
                    true, true);

    std::string collateralAddress =         metaObj["collateralAddress"].getValStr();
    std::string operatorAuthAddressBase58 = metaObj["operatorAuthAddress"].getValStr();

    // Parameters validation block
    CTxDestination collateralDest = DecodeDestination(collateralAddress);
    /// @todo @max implement WitnessV0KeyHash! (type 4)
    if (collateralDest.which() != 1 && collateralDest.which() != 4)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "collateralAddress (" + collateralAddress + ") does not refer to a P2PKH or P2WPKH address");
    }

    // owner and operator check
    CTxDestination destOperator = operatorAuthAddressBase58 == "" ? collateralDest : DecodeDestination(operatorAuthAddressBase58);
    /// @todo @max implement WitnessV0KeyHash! (type 4)
    PKHash const * operatorAuthAddress = boost::get<PKHash>(&destOperator);
    if (!operatorAuthAddress || operatorAuthAddress->IsNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorAuthAddress (" + operatorAuthAddressBase58 + ") does not refer to a P2KH address");
    }
    CKeyID operatorAuthKey = CKeyID(*operatorAuthAddress);
    if (pmasternodesview->ExistMasternode(CMasternodesView::AuthIndex::ByOwner, operatorAuthKey) ||
        pmasternodesview->ExistMasternode(CMasternodesView::AuthIndex::ByOperator, operatorAuthKey))
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Masternode with operatorAuthAddress already exists");
    }

    CDataStream metadata(MnTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(MasternodesTxType::CreateMasternode)
             << operatorAuthKey
             ;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    CMutableTransaction rawTx;
    rawTx.vout.push_back(CTxOut(EstimateMnCreationFee(), scriptMeta));
    rawTx.vout.push_back(CTxOut(GetMnCollateralAmount(), GetScriptForDestination(collateralDest)));

    return fundsignsend(rawTx, request);
}


UniValue mn_resign(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    RPCHelpMan{"mn_resign",
        "\nCreates (and submits to local node and network) a transaction resigning your masternode. Collateral will be unlocked after " + std::to_string(GetMnCollateralUnlockDelay()) + " blocks.\n"
        "The first optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the MN's owner (collateral) address" +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
            {"prevouts", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "A json array of json objects. Provide it if you want to spent specific UTXOs",
                {
                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                        },
                    },
                },
            },
            {"mn_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The Masternode's ID"},
        },
        RPCResult{
            "\"hex\"                  (string) The hex-encoded raw transaction with signature(s)\n"
        },
        RPCExamples{
            HelpExampleCli("mn_resign", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\" \"mn_id\"")
            + HelpExampleRpc("mn_resign", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\" \"mn_id\"")
        },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot resign Masternode while still in Initial Block Download");
    }

    // Make sure the results are valid at least up to the most recent block
    // the user could have gotten from another RPC command prior to now
    pwallet->BlockUntilSyncedToCurrentChain();

    auto locked_chain = pwallet->chain().lock();  /// @todo @maxb is it enough? (i mean cs_main)
    LOCK(pwallet->cs_wallet);

    EnsureWalletIsUnlocked(pwallet);

    RPCTypeCheck(request.params, { UniValue::VARR, UniValue::VSTR }, true);

    std::string const nodeIdStr = request.params[1].getValStr();
    uint256 nodeId = uint256S(nodeIdStr);
    auto optIDs = pmasternodesview->AmIOwner();
    if (!optIDs)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("You are not the owner of masternode %s, or it does not exist", nodeIdStr));
    }
    auto nodePtr = pmasternodesview->ExistMasternode(nodeId);
    if (nodePtr->resignTx != uint256())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Masternode %s was resigned by tx %s; collateral can be spend at block #%d", nodeIdStr, nodePtr->resignTx.GetHex(), nodePtr->resignHeight+GetMnActivationDelay()));
    }

    /// @todo @maxb get coin, check auth (script of coin) matching current wallet

    CDataStream metadata(MnTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(MasternodesTxType::ResignMasternode)
             << nodeId;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    CMutableTransaction rawTx;
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    return fundsignsend(rawTx, request);
}


// Here (but not a class method) just by similarity with other '..ToJSON'
UniValue mnToJSON(CMasternode const & node)
{
    UniValue ret(UniValue::VOBJ);
    /// @todo @max implement WitnessV0KeyHash! (type 4)

    ret.pushKV("ownerAuthAddress", EncodeDestination(PKHash(node.ownerAuthAddress)));
    ret.pushKV("operatorAuthAddress", EncodeDestination(PKHash(node.operatorAuthAddress)));

    ret.pushKV("height", static_cast<uint64_t>(node.height));
    ret.pushKV("resignHeight", static_cast<int>(node.resignHeight));

    ret.pushKV("resignTx", node.resignTx.GetHex());

    return ret;
}

UniValue dumpnode(uint256 const & id, CMasternode const & node, bool verbose)
{
    UniValue entry(UniValue::VOBJ);
    entry.pushKV("id", id.GetHex());
    entry.pushKV("status", node.GetHumanReadableStatus());
    if (verbose)
    {
        entry.pushKV("mn", mnToJSON(node));
    }
    return entry;
}

//UniValue mn_list(const JSONRPCRequest& request)
//{
//    if (request.fHelp || request.params.size() > 2)
//        throw std::runtime_error(
//                "mn_list [\"MN-id\",...]\n"
//                "\nReturns information about specified masternodes (or all, if list of ids is empty).\n"
//                "\nArguments:\n"
//                "1.   [\n"
//                "         \"id\"            (string, optional) A json array of masternode ids\n"
//                "       ,...\n"
//                "     ]\n"
//                "2. \"verbose\"             (bool, optional) Flag to specify verbose list\n"
//                "\nResult:\n"
//                "[{...},...]                (array) Json array of json objects with masternodes information\n"
//                "\nExamples\n"
//                + HelpExampleCli("mn_list", "\"[]\"")
//                + HelpExampleRpc("mn_list", "\"[]\"")
//    );

//    LOCK(cs_main);

//    RPCTypeCheck(request.params, { UniValue::VARR, UniValue::VBOOL }, true);

//    UniValue inputs(UniValue::VARR);
//    if (request.params.size() > 0)
//    {
//        inputs = request.params[0].get_array();
//    }
//    bool verbose = false;
//    if (request.params.size() > 1)
//    {
//        verbose = request.params[1].get_bool();
//    }

//    UniValue ret(UniValue::VARR);
//    CMasternodes const & mns = pmasternodesview->GetMasternodes();
//    if (inputs.empty())
//    {
//        // Dumps all!
//        for (auto it = mns.begin(); it != mns.end(); ++it)
//        {
//            ret.push_back(dumpnode(it->first, it->second, verbose));
//        }
//    }
//    else
//    {
//        for (size_t idx = 0; idx < inputs.size(); ++idx)
//        {
//            uint256 id = ParseHashV(inputs[idx], "masternode id");
//            auto const & node = pmasternodesview->ExistMasternode(id);
//            if (node)
//            {
//                ret.push_back(dumpnode(id, *node, verbose));
//            }
//        }
//    }
//    return ret;
//}


static const CRPCCommand commands[] =
{ //  category          name                        actor (function)            params
  //  ----------------- ------------------------    -----------------------     ----------
//  { "masternodes",    "mn_estimateannouncementfee", &mn_estimateannouncementfee, {}  },
  { "masternodes",      "mn_create",                &mn_create,                 { "inputs", "metadata" }  },
  { "masternodes",      "mn_resign",                &mn_resign,                 { "mn_id" }  },

//    { "masternodes",    "mn_list",                    &mn_list,                     true  },

};

void RegisterMasternodesRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
