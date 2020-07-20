// Copyright (c) 2020 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/masternodes.h>
#include <masternodes/criminals.h>
#include <masternodes/mn_checks.h>

#include <chainparams.h>
#include <core_io.h>
#include <consensus/validation.h>
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
#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>
//#endif

#include <stdexcept>

#include <boost/algorithm/string.hpp>
#include <boost/assign/list_of.hpp>
#include <rpc/rawtransaction_util.h>

extern UniValue createrawtransaction(UniValue const& params, bool fHelp); // in rawtransaction.cpp
extern UniValue fundrawtransaction(UniValue const& params, bool fHelp); // in rpcwallet.cpp
extern UniValue signrawtransaction(UniValue const& params, bool fHelp); // in rawtransaction.cpp
extern UniValue sendrawtransaction(UniValue const& params, bool fHelp); // in rawtransaction.cpp
extern UniValue getnewaddress(UniValue const& params, bool fHelp); // in rpcwallet.cpp
extern bool EnsureWalletIsAvailable(bool avoidException); // in rpcwallet.cpp
extern bool DecodeHexTx(CTransaction& tx, std::string const& strHexTx); // in core_io.h

extern void FundTransaction(CWallet* const pwallet, CMutableTransaction& tx, CAmount& fee_out, int& change_position,
                            UniValue options);

static CMutableTransaction fund(CMutableTransaction _mtx, JSONRPCRequest const& request, CWallet* const pwallet) {
    CMutableTransaction mtx = std::move(_mtx);
    CAmount fee_out;
    int change_position = mtx.vout.size();

    std::string strFailReason;
    CCoinControl coinControl;
    if (!pwallet->FundTransaction(mtx, fee_out, change_position, strFailReason, false /*lockUnspents*/,
                                  std::set<int>() /*setSubtractFeeFromOutputs*/, coinControl)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strFailReason);
    }
    return mtx;
}

static CTransactionRef
signsend(const CMutableTransaction& _mtx, JSONRPCRequest const& request, CWallet* const pwallet) {
    // sign
    JSONRPCRequest new_request;
    new_request.id = request.id;
    new_request.URI = request.URI;

    new_request.params.setArray();
    new_request.params.push_back(EncodeHexTx(CTransaction(_mtx)));
    UniValue txSigned = signrawtransactionwithwallet(new_request);

    // from "sendrawtransaction"
    {
        CMutableTransaction mtx;
        if (!DecodeHexTx(mtx, txSigned["hex"].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        CTransactionRef tx(MakeTransactionRef(std::move(mtx)));

        CAmount max_raw_tx_fee = {COIN / 10}; /// @todo check it with 0

        std::string err_string;
        AssertLockNotHeld(cs_main);
        const TransactionError err = BroadcastTransaction(tx, err_string, max_raw_tx_fee, /*relay*/
                                                          true, /*wait_callback*/ false);
        if (TransactionError::OK != err) {
            throw JSONRPCTransactionError(err, err_string);
        }
        return tx;
    }
}

static UniValue fundsignsend(CMutableTransaction mtx, JSONRPCRequest const& request, CWallet* const pwallet) {
    return signsend(fund(std::move(mtx), request, pwallet), request, pwallet)->GetHash().GetHex();
}

// returns either base58/bech32 address, or hex if format is unknown
std::string ScriptToString(CScript const& script) {
    CTxDestination dest;
    if (!ExtractDestination(script, dest)) {
        return script.GetHex();
    }
    return EncodeDestination(dest);
}
// decodes either base58/bech32 address, or a hex format
CScript DecodeScript(std::string const& str) {
    if (IsHex(str)) {
        const auto raw = ParseHex(str);
        return CScript{raw.begin(), raw.end()};
    }
    const auto dest = DecodeDestination(str);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "recipient (" + str + ") does not refer to any valid address");
    }
    return GetScriptForDestination(dest);
}

static CTokenAmount DecodeAmount(const CWallet* pwallet, UniValue const& amountUni, std::string const& name) {
    // decode amounts
    std::string strAmount;
    if (amountUni.isArray()) { // * amounts
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, name + ": expected single amount");
    } else if (amountUni.isNum()) { // legacy format for '0' token
        strAmount = amountUni.getValStr() + "@" + DCT_ID{0}.ToString();
    } else { // only 1 amount
        strAmount = amountUni.get_str();
    }
    return GuessTokenAmount(strAmount, pwallet->chain()).ValOrException(JSONRPCErrorThrower(RPC_INVALID_PARAMETER, name));
}

static CBalances DecodeAmounts(const CWallet* pwallet, UniValue const& amountsUni, std::string const& name) {
    // decode amounts
    CBalances amounts;
    if (amountsUni.isArray()) { // * amounts
        for (const auto& amountUni : amountsUni.get_array().getValues()) {
            amounts.Add(DecodeAmount(pwallet, amountUni, name));
        }
    } else {
        amounts.Add(DecodeAmount(pwallet, amountsUni, name));
    }
    return amounts;
}

// decodes recipients from formats:
// "addr": 123.0,
// "addr": "123.0@0",
// "addr": "123.0@DFI",
// "addr": ["123.0@DFI", "123.0@0", ...]
static std::map<CScript, CBalances> DecodeRecipients(const CWallet* pwallet, UniValue const& sendTo) {
    std::map<CScript, CBalances> recipients;
    for (const std::string& addr : sendTo.getKeys()) {
        // decode recipient
        const auto recipient = DecodeScript(addr);
        if (recipients.find(recipient) != recipients.end()) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, addr + ": duplicate recipient");
        }
        // decode amounts and substitute
        recipients[recipient] = DecodeAmounts(pwallet, sendTo[addr], addr);
    }
    return recipients;
}

CAmount EstimateMnCreationFee() {
    // Current height + (1 day blocks) to avoid rejection;
    int targetHeight = ::ChainActive().Height() + 1 + (60 * 60 / Params().GetConsensus().pos.nTargetSpacing);
    return GetMnCreationFee(targetHeight);
}

std::vector<CTxIn> GetInputs(UniValue const& inputs) {
    std::vector<CTxIn> vin{};
    for (unsigned int idx = 0; idx < inputs.size(); idx++) {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        vin.push_back(CTxIn(txid, nOutput));
    }
    return vin;
}

static std::vector<CTxIn> GetAuthInputs(CWallet* const pwallet, CTxDestination const& auth, UniValue const& explicitInputs) {
    std::vector<CTxIn> vin{};
    if (!explicitInputs.empty()) {
        return GetInputs(explicitInputs.get_array());
    } else {
        std::vector<COutput> vecOutputs;
        CCoinControl cctl;
        cctl.m_avoid_address_reuse = false;
        cctl.m_min_depth = 1;
        cctl.m_max_depth = 999999999;
        cctl.matchDestination = auth;
        cctl.m_tokenFilter = {DCT_ID{0}};

        pwallet->BlockUntilSyncedToCurrentChain();
        auto locked_chain = pwallet->chain().lock();
        LOCK(pwallet->cs_wallet);

        pwallet->AvailableCoins(*locked_chain, vecOutputs, true, &cctl, 1, MAX_MONEY, MAX_MONEY, 1);

        if (vecOutputs.empty()) {
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strprintf(
                    "Can't find any UTXO's for owner. Are you an owner? If so, send some coins to address %s and try again!",
                    EncodeDestination(auth)));
        }
        vin.push_back(CTxIn(vecOutputs[0].tx->GetHash(), vecOutputs[0].i));
    }
    return vin;
}

static CWallet* GetWallet(const JSONRPCRequest& request) {
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    EnsureWalletIsAvailable(pwallet, false);
    EnsureWalletIsUnlocked(pwallet);
    return pwallet;
}

/*
 *
 *  Issued by: any
*/
UniValue createmasternode(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"createmasternode",
               "\nCreates (and submits to local node and network) a masternode creation transaction with given metadata.\n"
               "The first optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects",
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
                                {"operatorAuthAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                 "Masternode operator auth address (P2PKH only, unique)"},
                                {"collateralAddress", RPCArg::Type::STR, RPCArg::Optional::NO,
                                 "Any valid address for keeping collateral amount (any P2PKH or P2WKH address) - used as owner key"},
                        },
                       },
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("createmasternode", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\" "
                                                          "\"{\\\"operatorAuthAddress\\\":\\\"address\\\","
                                                          "\\\"collateralAddress\\\":\\\"address\\\""
                                                          "}\"")
                       + HelpExampleRpc("createmasternode", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\" "
                                                            "\"{\\\"operatorAuthAddress\\\":\\\"address\\\","
                                                            "\\\"collateralAddress\\\":\\\"address\\\""
                                                            "}\"")
               },
    }.Check(request);


    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create Masternode while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VOBJ}, true);
    if (request.params[0].isNull() || request.params[1].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 and 2 must be non-null, and argument 2 expected as object with "
                           "{\"operatorAuthAddress\",\"collateralAddress\"}");
    }
    UniValue metaObj = request.params[1].get_obj();
    RPCTypeCheckObj(metaObj, {
                            {"operatorAuthAddress", UniValue::VSTR},
                            {"collateralAddress",   UniValue::VSTR}
                    },
                    true, true);

    std::string collateralAddress = metaObj["collateralAddress"].getValStr();
    std::string operatorAuthAddressBase58 = metaObj["operatorAuthAddress"].getValStr();

    CTxDestination collateralDest = DecodeDestination(collateralAddress);
    CTxDestination operatorDest = operatorAuthAddressBase58 == "" ? collateralDest : DecodeDestination(operatorAuthAddressBase58);
    CKeyID operatorAuthKey = operatorDest.which() == 1 ? CKeyID(*boost::get<PKHash>(&operatorDest)) : CKeyID(*boost::get<WitnessV0KeyHash>(&operatorDest));

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateMasternode)
             << static_cast<char>(operatorDest.which()) << operatorAuthKey;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    CMutableTransaction rawTx;

    rawTx.vin = GetInputs(request.params[0].get_array());

    rawTx.vout.push_back(CTxOut(EstimateMnCreationFee(), scriptMeta));
    rawTx.vout.push_back(CTxOut(GetMnCollateralAmount(), GetScriptForDestination(collateralDest)));

    rawTx = fund(rawTx, request, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyCreateMasternodeTx(mnview_dummy, CTransaction(rawTx), ::ChainActive().Tip()->height + 1,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, static_cast<char>(operatorDest.which()), operatorAuthKey}));
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, request, pwallet)->GetHash().GetHex();
}


UniValue resignmasternode(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"resignmasternode",
               "\nCreates (and submits to local node and network) a transaction resigning your masternode. Collateral will be unlocked after " +
               std::to_string(GetMnResignDelay()) + " blocks.\n"
                                                    "The first optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the MN's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects. Provide it if you want to spent specific UTXOs",
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
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("resignmasternode", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\" \"mn_id\"")
                       + HelpExampleRpc("resignmasternode", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\" \"mn_id\"")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot resign Masternode while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VSTR}, true);

    std::string const nodeIdStr = request.params[1].getValStr();
    uint256 nodeId = uint256S(nodeIdStr);
    CTxDestination ownerDest;

    {
        LOCK(cs_main);
        auto nodePtr = pcustomcsview->GetMasternode(nodeId);
        ownerDest = nodePtr->ownerType == 1 ? CTxDestination(PKHash(nodePtr->ownerAuthAddress)) : CTxDestination(
                WitnessV0KeyHash(nodePtr->ownerAuthAddress));
    }

    CMutableTransaction rawTx;
    rawTx.vin = GetAuthInputs(pwallet, ownerDest, request.params[0].get_array());

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ResignMasternode)
             << nodeId;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    rawTx = fund(rawTx, request, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyResignMasternodeTx(mnview_dummy, ::ChainstateActive().CoinsTip(), CTransaction(rawTx), ::ChainActive().Tip()->height + 1,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, nodeId}));
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, request, pwallet)->GetHash().GetHex();
}


// Here (but not a class method) just by similarity with other '..ToJSON'
UniValue mnToJSON(uint256 const & nodeId, CMasternode const& node, bool verbose) {
    UniValue ret(UniValue::VOBJ);
    if (!verbose) {
        ret.pushKV(nodeId.GetHex(), CMasternode::GetHumanReadableState(node.GetState()));
    }
    else {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("ownerAuthAddress", EncodeDestination(
                node.ownerType == 1 ? CTxDestination(PKHash(node.ownerAuthAddress)) : CTxDestination(
                        WitnessV0KeyHash(node.ownerAuthAddress))));
        obj.pushKV("operatorAuthAddress", EncodeDestination(
                node.operatorType == 1 ? CTxDestination(PKHash(node.operatorAuthAddress)) : CTxDestination(
                        WitnessV0KeyHash(node.operatorAuthAddress))));

        obj.pushKV("creationHeight", node.creationHeight);
        obj.pushKV("resignHeight", node.resignHeight);
        obj.pushKV("resignTx", node.resignTx.GetHex());
        obj.pushKV("banHeight", node.banHeight);
        obj.pushKV("banTx", node.banTx.GetHex());
        obj.pushKV("state", CMasternode::GetHumanReadableState(node.GetState()));
        obj.pushKV("mintedBlocks", (uint64_t) node.mintedBlocks);

        /// @todo add unlock height and|or real resign height
        ret.pushKV(nodeId.GetHex(), obj);
    }
    return ret;
}

UniValue listmasternodes(const JSONRPCRequest& request) {
    RPCHelpMan{"listmasternodes",
               "\nReturns information about specified masternodes (or all, if list of ids is empty).\n",
               {
                        {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                         {
                                 {"start", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED,
                                  "Optional first key to iterate from, in lexicographical order."
                                  "Typically it's set to last ID from previous request."},
                                 {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                  "If true, then iterate including starting position. False by default"},
                                 {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Maximum number of orders to return, 100 by default"},
                         },
                        },
                        {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                    "Flag for verbose list (default = true), otherwise only ids are listed"},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with masternodes information\n"
               },
               RPCExamples{
                       HelpExampleCli("listmasternodes", "\"[mn_id]\" False")
                       + HelpExampleRpc("listmasternodes", "\"[mn_id]\" False")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].get_bool();
    }
    // parse pagination
    size_t limit = 100;
    uint256 start = {};
    {
        if (request.params.size() > 0) {
            bool including_start = false;
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                start = ParseHashV(paginationObj["start"], "start");
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                start = ArithToUint256(UintToArith256(start) + arith_uint256{1});
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    UniValue ret(UniValue::VOBJ);

    LOCK(cs_main);
    pcustomcsview->ForEachMasternode([&](uint256 const& nodeId, CMasternode& node) {
        ret.pushKVs(mnToJSON(nodeId, node, verbose));
        limit--;
        return limit != 0;
    }, start);

    return ret;
}

UniValue getmasternode(const JSONRPCRequest& request) {
    RPCHelpMan{"getmasternode",
               "\nReturns information about specified masternode.\n",
               {
                       {"mn_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Masternode's id"},
               },
               RPCResult{
                       "{id:{...}}     (object) Json object with masternode information\n"
               },
               RPCExamples{
                       HelpExampleCli("getmasternode", "\"mn_id\"")
                       + HelpExampleRpc("getmasternode", "\"mn_id\"")
               },
    }.Check(request);

    uint256 id = ParseHashV(request.params[0], "masternode id");

    LOCK(cs_main);
    auto node = pcustomcsview->GetMasternode(id);
    if (node) {
        return mnToJSON(id, *node, true); // or maybe just node, w/o id?
    }
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Masternode not found");
}

UniValue listcriminalproofs(const JSONRPCRequest& request) {
    RPCHelpMan{"listcriminalproofs",
               "\nReturns information about criminal proofs (pairs of signed blocks by one MN from different forks).\n",
               {
                    {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                         {
                             {"start", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED,
                              "Optional first key to iterate from, in lexicographical order."
                              "Typically it's set to last ID from previous request."},
                             {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                              "If true, then iterate including starting position. False by default"},
                             {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                              "Maximum number of orders to return, 100 by default"},
                         },
                    },
               },
               RPCResult{
                       "{id:{block1, block2},...}     (array) Json objects with block pairs\n"
               },
               RPCExamples{
                       HelpExampleCli("listcriminalproofs", "")
                       + HelpExampleRpc("listcriminalproofs", "")
               },
    }.Check(request);

    // parse pagination
    size_t limit = 100;
    uint256 start = {};
    {
        if (request.params.size() > 0) {
            bool including_start = false;
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                start = ParseHashV(paginationObj["start"], "start");
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                start = ArithToUint256(UintToArith256(start) + arith_uint256{1});
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);
    auto const proofs = pcriminals->GetUnpunishedCriminals();
    for (auto it = proofs.lower_bound(start); it != proofs.end() && limit != 0; ++it, --limit) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("hash1", it->second.blockHeader.GetHash().ToString());
        obj.pushKV("height1", it->second.blockHeader.height);
        obj.pushKV("hash2", it->second.conflictBlockHeader.GetHash().ToString());
        obj.pushKV("height2", it->second.conflictBlockHeader.height);
        obj.pushKV("mintedBlocks", it->second.blockHeader.mintedBlocks);
        ret.pushKV(it->first.ToString(), obj);
    }
    return ret;
}

UniValue createtoken(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"createtoken",
               "\nCreates (and submits to local node and network) a token creation transaction with given metadata.\n"
               "The first optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects",
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
                                {"symbol", RPCArg::Type::STR, RPCArg::Optional::NO,
                                 "Token's symbol (unique), no longer than " +
                                 std::to_string(CToken::MAX_TOKEN_SYMBOL_LENGTH)},
                                {"name", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                 "Token's name (optional), no longer than " +
                                 std::to_string(CToken::MAX_TOKEN_NAME_LENGTH)},
                                {"decimal", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                 "Token's decimal places (optional, fixed to 8 for now, unchecked)"},
                                {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                 "Token's total supply limit (optional, zero for now, unchecked)"},
                                {"mintable", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                 "Token's 'Mintable' property (bool, optional), fixed to 'True' for now"},
                                {"tradeable", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                 "Token's 'Tradeable' property (bool, optional), fixed to 'True' for now"},
                                {"collateralAddress", RPCArg::Type::STR, RPCArg::Optional::NO,
                                 "Any valid destination for keeping collateral amount - used as token's owner auth"},
                        },
                       },
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("createtoken", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\" "
                                                          "\"{\\\"symbol\\\":\\\"MyToken\\\","
                                                          "\\\"collateralAddress\\\":\\\"address\\\""
                                                          "}\"")
                       + HelpExampleRpc("createtoken", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\" "
                                                            "\"{\\\"symbol\\\":\\\"MyToken\\\","
                                                            "\\\"collateralAddress\\\":\\\"address\\\""
                                                            "}\"")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create token while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VOBJ}, true);
    if (request.params[0].isNull() || request.params[1].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 and 2 must be non-null, and argument 2 expected as object at least with "
                           "{\"symbol\",\"collateralDest\"}");
    }
    UniValue metaObj = request.params[1].get_obj();

    std::string collateralAddress = metaObj["collateralAddress"].getValStr();
    CTxDestination collateralDest = DecodeDestination(collateralAddress);
    if (collateralDest.which() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "collateralAddress (" + collateralAddress + ") does not refer to any valid address");
    }

    int height{0};
    {
        LOCK(cs_main);
        height = ::ChainActive().Tip()->height + 1;
    }

    CToken token;
    token.symbol = trim_ws(metaObj["symbol"].getValStr()).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    token.name = trim_ws(metaObj["name"].getValStr()).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
//    token.decimal = metaObj["name"].get_int(); // fixed for now, check range later
//    token.limit = metaObj["limit"].get_int(); // fixed for now, check range later
//    token.flags = metaObj["mintable"].get_bool() ? token.flags | CToken::TokenFlags::Mintable : token.flags; // fixed for now, check later
//    token.flags = metaObj["tradeable"].get_bool() ? token.flags | CToken::TokenFlags::Tradeable : token.flags; // fixed for now, check later

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateToken)
             << token;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    CMutableTransaction rawTx;

    rawTx.vin = GetInputs(request.params[0].get_array());

    rawTx.vout.push_back(CTxOut(GetTokenCreationFee(height), scriptMeta));
    rawTx.vout.push_back(CTxOut(GetTokenCollateralAmount(), GetScriptForDestination(collateralDest)));

    rawTx = fund(rawTx, request, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyCreateTokenTx(mnview_dummy, CTransaction(rawTx), height,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, token}));
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, request, pwallet)->GetHash().GetHex();
}

UniValue destroytoken(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"destroytoken",
               "\nCreates (and submits to local node and network) a transaction destroying your token. Collateral will be unlocked.\n"
               "The first optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the token's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects. Provide it if you want to spent specific UTXOs",
                        {
                                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                 {
                                         {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                         {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                 },
                                },
                        },
                       },
                       {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "The tokens's symbol, id or creation tx"},
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("destroytoken", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\" \"symbol\"")
                       + HelpExampleRpc("destroytoken", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\" \"symbol\"")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot resign Masternode while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VSTR}, true);

    std::string const tokenStr = trim_ws(request.params[1].getValStr());
    CTxDestination ownerDest;
    uint256 creationTx{};
    {
        LOCK(cs_main);
        DCT_ID id;
        auto token = pcustomcsview->GetTokenGuessId(tokenStr, id);
        if (!token) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenStr));
        }
        if (id < CTokensView::DCT_ID_START) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s is a 'stable coin'", tokenStr));
        }
        LOCK(pwallet->cs_wallet);
        auto tokenImpl = static_cast<CTokenImplementation const& >(*token);
        auto wtx = pwallet->GetWalletTx(tokenImpl.creationTx);
        if (!wtx || !ExtractDestination(wtx->tx->vout[1].scriptPubKey, ownerDest)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               strprintf("Can't extract destination for token's %s collateral", tokenStr));
        }
        creationTx = tokenImpl.creationTx;
    }

    CMutableTransaction rawTx;

    rawTx.vin = GetAuthInputs(pwallet, ownerDest, request.params[0].get_array());

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::DestroyToken)
             << creationTx;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    rawTx = fund(rawTx, request, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyDestroyTokenTx(mnview_dummy, ::ChainstateActive().CoinsTip(), CTransaction(rawTx), ::ChainActive().Tip()->height + 1,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, creationTx}));
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, request, pwallet)->GetHash().GetHex();
}

UniValue tokenToJSON(DCT_ID const& id, CToken const& token, bool verbose) {
    UniValue tokenObj(UniValue::VOBJ);
    tokenObj.pushKV("symbol", token.symbol);
    tokenObj.pushKV("name", token.name);
    if (verbose) {
        tokenObj.pushKV("decimal", token.decimal);
        tokenObj.pushKV("limit", token.limit);
        tokenObj.pushKV("mintable", token.IsMintable());
        tokenObj.pushKV("tradeable", token.IsTradeable());
        if (id >= CTokensView::DCT_ID_START) {
            CTokenImplementation const& tokenImpl = static_cast<CTokenImplementation const&>(token);
            tokenObj.pushKV("creationTx", tokenImpl.creationTx.ToString());
            tokenObj.pushKV("creationHeight", tokenImpl.creationHeight);
            tokenObj.pushKV("destructionTx", tokenImpl.destructionTx.ToString());
            tokenObj.pushKV("destructionHeight", tokenImpl.destructionHeight);
            /// @todo tokens: collateral address/script
//            tokenObj.pushKV("collateralAddress", tokenImpl.destructionHeight);
        }
    }
    UniValue ret(UniValue::VOBJ);
    ret.pushKV(id.ToString(), tokenObj);
    return ret;
}

// @todo implement pagination, similar to list* calls below
UniValue listtokens(const JSONRPCRequest& request) {
    RPCHelpMan{"listtokens",
               "\nReturns information about tokens.\n",
               {
                        {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                            {
                                 {"start", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Optional first key to iterate from, in lexicographical order."
                                  "Typically it's set to last ID from previous request."},
                                 {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                  "If true, then iterate including starting position. False by default"},
                                 {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Maximum number of tokens to return, 100 by default"},
                            },
                        },
                        {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                    "Flag for verbose list (default = true), otherwise only ids, symbols and names are listed"},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with tokens information\n"
               },
               RPCExamples{
                       HelpExampleCli("listtokens", "{\"start\":128} False")
                       + HelpExampleRpc("listtokens", "{\"start\":128} False")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].get_bool();
    }

    // parse pagination
    size_t limit = 100;
    DCT_ID start{0};
    {
        if (request.params.size() > 0) {
            bool including_start = false;
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                start.v = (uint32_t) paginationObj["start"].get_int();
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                ++start.v;
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);
    pcustomcsview->ForEachToken([&](DCT_ID const& id, CToken const& token) {
        ret.pushKVs(tokenToJSON(id, token, verbose));

        limit--;
        return limit != 0;
    }, start);

    return ret;
}

UniValue gettoken(const JSONRPCRequest& request) {
    RPCHelpMan{"gettoken",
               "\nReturns information about token.\n",
               {
                       {"key", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "One of the keys may be specified (id/symbol/creationTx)"},
               },
               RPCResult{
                       "{id:{...}}     (array) Json object with token information\n"
               },
               RPCExamples{
                       HelpExampleCli("gettoken", "GOLD")
                       + HelpExampleRpc("gettoken", "GOLD")
               },
    }.Check(request);

    LOCK(cs_main);

    DCT_ID id;
    auto token = pcustomcsview->GetTokenGuessId(request.params[0].getValStr(), id);
    if (token) {
        return tokenToJSON(id, *token, true);
    }
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Token not found");
}

UniValue minttokens(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"minttokens",
               "\nCreates (and submits to local node and network) a transaction minting your token. \n"
               "The first optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the token's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects. Provide it if you want to spent specific UTXOs",
                        {
                                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                 {
                                         {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                         {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                 },
                                },
                        },
                       },
                       {"symbol", RPCArg::Type::STR, RPCArg::Optional::NO, "The tokens's symbol"},
                       {"amounts", RPCArg::Type::OBJ, RPCArg::Optional::NO, "A json object with addresses and amounts",
                        {
                                {"address", RPCArg::Type::AMOUNT, RPCArg::Optional::NO,
                                 "The defi address is the key, the numeric amount (can be string) in " + CURRENCY_UNIT +
                                 " is the value"},
                        },
                       },
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("minttokens",
                                      "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\" \"symbol\"")  /// @todo tokens: modify
                       + HelpExampleRpc("minttokens", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\" \"symbol\"")
               },
    }.Check(request);

//    throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Temporary OFF until wallet with tokens completely implemented!");

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot resign Masternode while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VSTR}, true);

    std::string const symbol = request.params[1].getValStr();
    UniValue sendTo = request.params[2].get_obj();

    CTxDestination ownerDest;
    DCT_ID tokenId{};
    {
        auto locked_chain = pwallet->chain().lock();
        auto pair = pcustomcsview->GetToken(symbol);
        if (!pair) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", symbol));
        }
        if (pair->first < CTokensView::DCT_ID_START) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s is a 'stable coin'", symbol));
        }
        auto token = static_cast<CTokenImplementation const& >(*pair->second);
        if (token.destructionTx != uint256{}) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               strprintf("Token %s already destroyed at height %i by tx %s", symbol,
                                         token.destructionHeight, token.destructionTx.GetHex()));
        }
        LOCK(pwallet->cs_wallet);
        auto wtx = pwallet->GetWalletTx(token.creationTx);
        if (!wtx || !ExtractDestination(wtx->tx->vout[1].scriptPubKey, ownerDest)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               strprintf("Can't extract destination for token's %s collateral", symbol));
        }
        tokenId = pair->first;
    }

    // @todo use DecodeRecipients instead

    std::set<CTxDestination> destinations; // just for duplication control
    std::vector<CTxOut> vecSend;

    std::vector<std::string> keys = sendTo.getKeys();
    for (const std::string& name_ : keys) {
        CTxDestination dest = DecodeDestination(name_);
        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Defi address: ") + name_);
        }

        if (destinations.count(dest)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated address: ") + name_);
        }
        destinations.insert(dest);

        CScript scriptPubKey = GetScriptForDestination(dest);
        CAmount nAmount = AmountFromValue(sendTo[name_]);
        if (nAmount <= 0)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

        vecSend.push_back(CTxOut(nAmount, scriptPubKey, tokenId));
    }

    CMutableTransaction rawTx;

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::MintToken);

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vin = GetAuthInputs(pwallet, ownerDest, request.params[0].get_array());

    rawTx.vout.push_back(CTxOut(0, scriptMeta));
    rawTx.vout.insert(rawTx.vout.end(), vecSend.begin(), vecSend.end());

    // Now try to fund and sign manually:

    CTransactionRef tx_new;
    {
        CCoinControl coinControl;
        coinControl.fAllowOtherInputs = true;

        for (const CTxIn& txin : rawTx.vin) {
            coinControl.Select(txin.prevout);
        }

        auto locked_chain = pwallet->chain().lock();
        LOCK(pwallet->cs_wallet);
        CAmount nFeeRet;
        std::string strFailReason;
        int changePos = rawTx.vout.size();

        if (!pwallet->CreateMintTokenTransaction(*locked_chain, rawTx, tx_new, nFeeRet, changePos, strFailReason,
                                                 coinControl)) {
            throw JSONRPCError(RPC_WALLET_ERROR, strFailReason);
        }

    }
    CAmount max_raw_tx_fee = {COIN / 10}; /// @todo check it with 0
    std::string err_string;
    AssertLockNotHeld(cs_main);
    const TransactionError err = BroadcastTransaction(tx_new, err_string, max_raw_tx_fee, /*relay*/
                                                      true, /*wait_callback*/ false);
    if (TransactionError::OK != err) {
        throw JSONRPCTransactionError(err, err_string);
    }

    return tx_new->GetHash().GetHex();
}

UniValue orderToJSON(uint256 const& id, COrder const& val, bool verbose) {
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("txid", id.ToString());
    if (verbose) {
        UniValue ownerObj(UniValue::VOBJ);
        ScriptPubKeyToUniv(val.owner, ownerObj, true);
        obj.pushKV("owner", ownerObj);
        obj.pushKV("give", val.give.ToString());
        obj.pushKV("take", val.take.ToString());
        obj.pushKV("premium", val.premium.ToString());
        obj.pushKV("creationHeight", (uint64_t) val.creationHeight);
        obj.pushKV("timeInForce", (uint64_t) val.timeInForce);
    }
    return obj;
}

UniValue createorder(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    auto h = RPCHelpMan{"createorder",
               "\nCreates (and submits to local node and network) an order creation transaction with given metadata.\n"
               "The first optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects",
                        {
                                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                 {
                                         {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                         {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                 },
                                },
                        },
                       },
                       {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                                {"give", RPCArg::Type::STR, RPCArg::Optional::NO,
                                 "Tokens to sell (offer) in \"amount@token\" format"},
                                {"take", RPCArg::Type::STR, RPCArg::Optional::NO,
                                 "Tokens to buy (receive) in \"amount@token\" format"},
                                {"premium", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                 "Optional premium to offer in \"amount@token\" format"},
                                {"timeinforce", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                 "Optional number of blocks for which order is active"},
                                {"owner", RPCArg::Type::STR, RPCArg::Optional::NO,
                                 "Any valid destination which will own the order"},
                        },
                       },
               },
               RPCResult{
                        "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("createorder", "\"[]\" "
                                                     "\"{\\\"give\\\":\\\"1.0@BTC\\\","
                                                     "\\\"take\\\":\\\"15.0@DFI\\\","
                                                     "\\\"premium\\\":\\\"0.00001@BTC\\\","
                                                     "\\\"owner\\\":\\\"address\\\""
                                                     "}\"")
               },
    };
    h.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VOBJ}, false);
    UniValue metaObj = request.params[1].get_obj();
    if (metaObj["owner"].isNull() || metaObj["give"].isNull() || metaObj["take"].isNull()) {
        throw std::runtime_error(h.ToString());
    }

    // decode amounts
    CCreateOrderMessage msg{};
    msg.take = DecodeAmount(pwallet, metaObj["take"], "take");
    msg.give = DecodeAmount(pwallet, metaObj["give"], "give");
    if (!metaObj["premium"].isNull()) {
        msg.premium = DecodeAmount(pwallet, metaObj["premium"], "premium");
    }
    if (!metaObj["timeinforce"].isNull()) {
        msg.timeInForce = (uint32_t) metaObj["timeinforce"].get_int();
    }

    // decode owner
    msg.owner = DecodeScript(metaObj["owner"].get_str());

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::CreateOrder)
             << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    CMutableTransaction rawTx;
    rawTx.vout.push_back(CTxOut(0, scriptMeta));
    CTxDestination ownerDest;
    if (!ExtractDestination(msg.owner, ownerDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid owner destination");
    }
    rawTx.vin = GetAuthInputs(pwallet, ownerDest, request.params[0].get_array());

    // fund
    rawTx = fund(rawTx, request, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyCreateOrderTx(mnview_dummy, g_chainstate->CoinsTip(), CTransaction(rawTx),
                                      ::ChainActive().Tip()->height + 1,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}));
        if (!res.ok) {
            if (res.code == CustomTxErrCodes::NotEnoughBalance) {
                throw JSONRPCError(RPC_INVALID_REQUEST,
                                   "Execution test failed: not enough balance on owner's account, call utxostoaccount to increase it.\n" +
                                   res.msg);
            }
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }

    return signsend(rawTx, request, pwallet)->GetHash().GetHex();
}

UniValue destroyorder(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    auto h = RPCHelpMan{"destroyorder",
               "\nCreates (and submits to local node and network) an order destruction transaction with given metadata.\n"
               "The first optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects",
                        {
                                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                 {
                                         {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                         {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                 },
                                },
                        },
                       },
                       {"order_txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                        "Txid of the order transaction to destroy"},
                       {"owner", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED,
                        "Order owner address. Not required if order is expired."},
               },
               RPCResult{
                        "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("destroyorder", "[] order_txid owner_address")
               },
    };
    h.Check(request);
    if (request.params.size() < 2) {
        throw std::runtime_error(h.ToString());
    }

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    // decode params
    uint256 msg{};
    msg = ParseHashV(request.params[1], "order_txid");

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::DestroyOrder)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    CMutableTransaction rawTx;
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    // add authentication if requested
    if (request.params.size() > 2) {
        CTxDestination ownerDest = DecodeDestination(request.params[2].get_str());
        if (!IsValidDestination(ownerDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "owner (" + request.params[2].get_str() + ") does not refer to any valid address");
        }
        rawTx.vin = GetAuthInputs(pwallet, ownerDest, request.params[0].get_array());
    }

    // fund
    rawTx = fund(rawTx, request, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyDestroyOrderTx(mnview_dummy, ::ChainstateActive().CoinsTip(), CTransaction(rawTx),
                                             ::ChainActive().Tip()->height + 1,
                                             ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}));
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }

    return signsend(rawTx, request, pwallet)->GetHash().GetHex();
}

UniValue matchorders(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    auto h = RPCHelpMan{"matchorders",
               "\nCreates (and submits to local node and network) an order marching transaction with given metadata.\n"
               "The first optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects",
                        {
                                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                 {
                                         {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                         {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                 },
                                },
                        },
                       },
                       {"order_alice", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                        "Txid of the order transaction to match"},
                       {"order_carol", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                        "Txid of the order transaction to match"},
                       {"matcher", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "Any valid destination which will take marching rewards"},
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("matchorders", "[] matcher_address order1_txid "
                                                         "order2_txid")
               },
    };
    h.Check(request);
    if (request.params.size() < 4) {
        throw std::runtime_error(h.ToString());
    }

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VSTR, UniValue::VSTR, UniValue::VSTR}, false);

    // decode params
    CMatchOrdersMessage msg{};
    msg.aliceOrderTx = ParseHashV(request.params[1], "order_alice");
    msg.carolOrderTx = ParseHashV(request.params[2], "order_carol");
    msg.matcher = DecodeScript(request.params[3].get_str());

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::MatchOrders)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    CMutableTransaction rawTx;
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    // fund
    rawTx = fund(rawTx, request, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyMatchOrdersTx(mnview_dummy, CTransaction(rawTx),
                                            ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}));
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }

    return signsend(rawTx, request, pwallet)->GetHash().GetHex();
}

UniValue matchordersinfo(const JSONRPCRequest& request) {
    RPCHelpMan{"matchordersinfo",
               "\nReturns estimation of orders matching outcome.\n",
               {
                       {"order_alice", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                        "Txid of the order transaction to match"},
                       {"order_carol", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                        "Txid of the order transaction to match"},
               },
               RPCResult{
                       "{...}     (array) Json object with matching information\n"
               },
               RPCExamples{
                       HelpExampleCli("matchordersinfo", "order1_txid "
                                                  "order2_txid")
               },
    }.Check(request);

    const uint256 order_alice = ParseHashV(request.params[0], "order_alice");
    const uint256 order_carol = ParseHashV(request.params[1], "order_carol");

    // calculate the math of matching
    LOCK(cs_main);
    const auto resV = GetMatchOrdersInfo(*pcustomcsview, CMatchOrdersMessage{order_alice, order_carol, CScript{}});
    if (!resV.ok) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Order wasn't matched: " + resV.msg);
    }

    // receipt to JSON
    auto& receipt = resV.val->first;
    UniValue ret(UniValue::VOBJ);
    UniValue matcherTake(UniValue::VARR);
    for (const auto& kv : receipt.matcherTake.balances) {
        matcherTake.push_back(CTokenAmount{kv.first, kv.second}.ToString());
    }
    UniValue alice(UniValue::VOBJ);
    alice.pushKV("take", receipt.alice.take.ToString());
    alice.pushKV("give", receipt.alice.give.ToString());
    alice.pushKV("premiumGive", receipt.alice.premiumGive.ToString());
    UniValue carol(UniValue::VOBJ);
    carol.pushKV("take", receipt.carol.take.ToString());
    carol.pushKV("give", receipt.carol.give.ToString());
    carol.pushKV("premiumGive", receipt.carol.premiumGive.ToString());

    ret.pushKV("matcherTake", matcherTake);
    ret.pushKV("alice", alice);
    ret.pushKV("carol", carol);

    return ret;
}

UniValue listorders(const JSONRPCRequest& request) {
    RPCHelpMan{"listorders",
               "\nReturns information about orders.\n",
               {
                       {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                                {"start", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED,
                                 "Optional first key to iterate from, in lexicographical order."
                                 "Typically it's set to last ID from previous request."},
                                {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                 "If true, then iterate including starting position. False by default"},
                                {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                 "Maximum number of orders to return, 100 by default"},
                        },
                       },
                       {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                   "Flag for verbose list (default = true), otherwise only ids are listed"},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with orders information\n"
               },
               RPCExamples{
                       HelpExampleCli("listorders", "")
                       + HelpExampleRpc("listorders", "False")
                         + HelpExampleRpc("listorders", "True"
                                                        "'{\"start\":\"34d9dae59f94bf3922a5af934dbfea810c24e6416683301aebb67272675c6109\","
                                                        "\"limit\":\"1000\""
                                                        "}'")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].get_bool();
    }
    // parse pagination
    size_t limit = 100;
    uint256 start = {};
    {
        if (request.params.size() > 0) {
            bool including_start = false;
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                start = ParseHashV(paginationObj["start"], "start");
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                start = ArithToUint256(UintToArith256(start) + arith_uint256{1});
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    UniValue ret(UniValue::VARR);

    LOCK(cs_main);
    pcustomcsview->ForEachOrder([&](uint256 const& txid, COrder const & order) {
        ret.push_back(orderToJSON(txid, order, verbose));

        limit--;
        return limit != 0;
    }, start);

    return ret;
}

UniValue getorder(const JSONRPCRequest& request) {
    RPCHelpMan{"getorder",
               "\nReturns information about orders.\n",
               {
                       {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO,
                        "Txid of the order transaction"},
               },
               RPCResult{
                       "{...}     (array) Json object with order information\n"
               },
               RPCExamples{
                       HelpExampleCli("getorder", "order_txid")
               },
    }.Check(request);

    const uint256 id = ParseHashV(request.params[0], "txid");

    LOCK(cs_main);
    const auto val = pcustomcsview->GetOrder(id);
    if (val) {
        return orderToJSON(id, *val, true);
    }
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Order not found");
}

CScript hexToScript(std::string const& str) {
    if (!IsHex(str)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "(" + str + ") doesn't represent a correct hex:\n");
    }
    const auto raw = ParseHex(str);
    return CScript{raw.begin(), raw.end()};
}

BalanceKey decodeBalanceKey(std::string const& str) {
    const auto pair = SplitTokenAddress(str);
    DCT_ID tokenID{};
    if (!pair.second.empty()) {
        auto id = DCT_ID::FromString(pair.second);
        if (!id.ok) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "(" + str + ") doesn't represent a correct balance key:\n" + id.msg);
        }
        tokenID = *id.val;
    }
    return {hexToScript(pair.first), tokenID};
}

UniValue accountToJSON(CScript const& owner, CTokenAmount const& amount, bool verbose) {
    // encode CScript into JSON
    UniValue ownerObj(UniValue::VOBJ);
    ScriptPubKeyToUniv(owner, ownerObj, true);
    if (!verbose) { // cut info
        if (ownerObj["addresses"].isArray() && !ownerObj["addresses"].get_array().empty()) {
            ownerObj = ownerObj["addresses"].get_array().getValues()[0];
        } else {
            ownerObj = {UniValue::VSTR};
            ownerObj.setStr(owner.GetHex());
        }
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("key", owner.GetHex() + "@" + amount.nTokenId.ToString());
    obj.pushKV("owner", ownerObj);
    obj.pushKV("amount", amount.ToString());
    return obj;
}

UniValue listaccounts(const JSONRPCRequest& request) {
    RPCHelpMan{"listaccounts",
               "\nReturns information about all accounts on chain.\n",
               {
                       {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                                {"start", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                 "Optional first key to iterate from, in lexicographical order."
                                 "Typically it's set to last ID from previous request."},
                                {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                 "If true, then iterate including starting position. False by default"},
                                {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                 "Maximum number of orders to return, 100 by default"},
                        },
                       },
                       {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                   "Flag for verbose list (default = true), otherwise limited objects are listed"}
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with accounts information\n"
               },
               RPCExamples{
                       HelpExampleCli("listaccounts", "")
                       + HelpExampleRpc("listaccounts", "{} False")
                       + HelpExampleRpc("listaccounts", "'{\"start\":\"a914b12ecde1759f792e0228e4fa6d262902687ca7eb87@0\","
                                                      "\"limit\":1000"
                                                      "}'")
               },
    }.Check(request);

    // parse pagination
    size_t limit = 100;
    BalanceKey start = {};
    {
        if (request.params.size() > 0) {
            bool including_start = false;
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                start = decodeBalanceKey(paginationObj["start"].get_str());
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                start.tokenID.v++;
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }
    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].get_bool();
    }

    UniValue ret(UniValue::VARR);

    LOCK(cs_main);
    pcustomcsview->ForEachBalance([&](CScript const & owner, CTokenAmount const & balance) {
        ret.push_back(accountToJSON(owner, balance, verbose));

        limit--;
        return limit != 0;
    }, start);

    return ret;
}

UniValue getaccount(const JSONRPCRequest& request) {
    RPCHelpMan{"getaccount",
               "\nReturns information about account.\n",
               {
                       {"owner", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "Owner address in base58/bech32/hex encoding"},
                       {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                                {"start", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                 "Optional first key to iterate from, in lexicographical order."
                                 "Typically it's set to last tokenID from previous request."},
                                {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                 "If true, then iterate including starting position. False by default"},
                                {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                 "Maximum number of orders to return, 100 by default"},
                        },
                       },
               },
               RPCResult{
                       "{...}     (array) Json object with order information\n"
               },
               RPCExamples{
                       HelpExampleCli("getaccount", "owner_address")
               },
    }.Check(request);

    // decode owner
    const auto reqOwner = DecodeScript(request.params[0].get_str());

    // parse pagination
    size_t limit = 100;
    DCT_ID start = {};
    {
        if (request.params.size() > 1) {
            bool including_start = false;
            UniValue paginationObj = request.params[1].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                start.v = (uint32_t) paginationObj["start"].get_int64();
            }
            if (!paginationObj["including_start"].isNull()) {
                including_start = paginationObj["including_start"].getBool();
            }
            if (!including_start) {
                start.v++;
            }
        }
        if (limit == 0) {
            limit = std::numeric_limits<decltype(limit)>::max();
        }
    }

    UniValue ret(UniValue::VARR);

    LOCK(cs_main);
    pcustomcsview->ForEachBalance([&](CScript const & owner, CTokenAmount const & balance) {
        if (owner != reqOwner) {
            return false;
        }

        ret.push_back(balance.ToString());

        limit--;
        return limit != 0;
    }, BalanceKey{reqOwner, start});

    return ret;
}

UniValue utxostoaccount(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"utxostoaccount",
               "\nCreates (and submits to local node and network) a transfer transaction from the wallet UTXOs to specfied account.\n"
               "The first optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects",
                        {
                                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                 {
                                         {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                         {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                 },
                                },
                        },
                       },
                       {"amounts", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                                {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address is the key, the value is amount in amount@token format. "
                                                                                     "If multiple tokens are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"},
                        },
                       },
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("utxostoaccount", "[] "
                                                     "'{\"address1\":\"1.0@DFI\","
                                                     "\"address2\":[\"2.0@BTC\", \"3.0@ETH\"]"
                                                     "}'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VOBJ}, false);

    // decode recipients
    CUtxosToAccountMessage msg{};
    msg.to = DecodeRecipients(pwallet, request.params[1].get_obj());

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::UtxosToAccount)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);
    CScript scriptBurn;
    scriptBurn << OP_RETURN;

    // burn
    const auto toBurn = SumAllTransfers(msg.to);
    if (toBurn.balances.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "zero amounts");
    }
    CMutableTransaction rawTx;
    for (const auto& kv : toBurn.balances) {
        if (rawTx.vout.empty()) { // first output is metadata
            rawTx.vout.push_back(CTxOut(kv.second, scriptMeta, kv.first));
        } else {
            rawTx.vout.push_back(CTxOut(kv.second, scriptBurn, kv.first));
        }
    }

    // fund
    rawTx = fund(rawTx, request, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyUtxosToAccountTx(mnview_dummy, CTransaction(rawTx),
                                               ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}));
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }

    return signsend(rawTx, request, pwallet)->GetHash().GetHex();
}

UniValue accounttoaccount(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"accounttoaccount",
               "\nCreates (and submits to local node and network) a transfer transaction from the specified account to the specfied accounts.\n"
               "The first optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects",
                        {
                                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                 {
                                         {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                         {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                 },
                                },
                        },
                       },
                       {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address of sender"},
                       {"to", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                                {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address is the key, the value is amount in amount@token format. "
                                                                                     "If multiple tokens are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"},
                        },
                       },
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("accounttoaccount", "[] sender_address "
                                                     "'{\"address1\":\"1.0@DFI\","
                                                     "\"address2\":[\"2.0@BTC\", \"3.0@ETH\"]"
                                                     "}'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VSTR, UniValue::VOBJ}, false);

    // decode sender and recipients
    CAccountToAccountMessage msg{};
    msg.from = DecodeScript(request.params[1].get_str());
    msg.to = DecodeRecipients(pwallet, request.params[2].get_obj());
    if (SumAllTransfers(msg.to).balances.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "zero amounts");
    }

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::AccountToAccount)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    CMutableTransaction rawTx;
    rawTx.vout.push_back(CTxOut(0, scriptMeta));
    CTxDestination ownerDest;
    if (!ExtractDestination(msg.from, ownerDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid owner destination");
    }
    rawTx.vin = GetAuthInputs(pwallet, ownerDest, request.params[0].get_array());

    // fund
    rawTx = fund(rawTx, request, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyAccountToAccountTx(mnview_dummy, g_chainstate->CoinsTip(), CTransaction(rawTx),
                                               ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}));
        if (!res.ok) {
            if (res.code == CustomTxErrCodes::NotEnoughBalance) {
                throw JSONRPCError(RPC_INVALID_REQUEST,
                                   "Execution test failed: not enough balance on owner's account, call utxostoaccount to increase it.\n" +
                                   res.msg);
            }
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }

    return signsend(rawTx, request, pwallet)->GetHash().GetHex();
}

UniValue accounttoutxos(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"accounttoutxos",
               "\nCreates (and submits to local node and network) a transfer transaction from the specified account to the specfied accounts.\n"
               "The first optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG,
                        "A json array of json objects",
                        {
                                {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                                 {
                                         {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                         {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                                 },
                                },
                        },
                       },
                       {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address of sender"},
                       {"to", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                                {"address", RPCArg::Type::STR, RPCArg::Optional::NO,
                                 "The defi address is the key, the value is amount in amount@token format. "
                                 "If multiple tokens are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"},
                        },
                       }
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("accounttoutxos", "[] sender_address 100@DFI")
                       + HelpExampleCli("accounttoutxos", "[] sender_address '[\"100@DFI\", \"200@BTC\", \"10000@129\"]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }

    RPCTypeCheck(request.params, {UniValue::VARR, UniValue::VSTR, UniValue::VOBJ}, false);

    // decode sender and recipients
    CAccountToUtxosMessage msg{};
    msg.from = DecodeScript(request.params[1].get_str());
    const auto to = DecodeRecipients(pwallet, request.params[2]);
    msg.balances = SumAllTransfers(to);
    if (msg.balances.balances.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "zero amounts");
    }

    // dummy encode, mintingOutputsStart isn't filled
    CScript scriptMeta;
    {
        std::vector<unsigned char> dummyMetadata(std::min((msg.balances.balances.size() * to.size()) * 40, (size_t)1024)); // heuristic to increse tx size before funding
        scriptMeta << OP_RETURN << dummyMetadata;
    }

    // auth
    CMutableTransaction rawTx;
    CTxDestination ownerDest;
    if (!ExtractDestination(msg.from, ownerDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid owner destination");
    }
    rawTx.vin = GetAuthInputs(pwallet, ownerDest, request.params[0].get_array());

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    // fund
    rawTx = fund(rawTx, request, pwallet);

    // re-encode with filled mintingOutputsStart
    {
        scriptMeta = {};
        msg.mintingOutputsStart = rawTx.vout.size();
        CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
        markedMetadata << static_cast<unsigned char>(CustomTxType::AccountToUtxos)
                       << msg;
        scriptMeta << OP_RETURN << ToByteVector(markedMetadata);
    }
    rawTx.vout[0].scriptPubKey = scriptMeta;

    // add outputs starting from mintingOutputsStart (must be unfunded, because it's minting)
    for (const auto& recip : to) {
        for (const auto& amount : recip.second.balances) {
            if (amount.second != 0) {
                rawTx.vout.push_back(CTxOut(amount.second, recip.first, amount.first));
            }
        }
    }

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyAccountToUtxosTx(mnview_dummy, g_chainstate->CoinsTip(), CTransaction(rawTx),
                                                 ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}));
        if (!res.ok) {
            if (res.code == CustomTxErrCodes::NotEnoughBalance) {
                throw JSONRPCError(RPC_INVALID_REQUEST,
                                   "Execution test failed: not enough balance on owner's account, call utxostoaccount to increase it.\n" +
                                   res.msg);
            }
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }

    return signsend(rawTx, request, pwallet)->GetHash().GetHex();
}

static const CRPCCommand commands[] =
        { //  category          name                        actor (function)            params
                //  ----------------- ------------------------    -----------------------     ----------
                {"masternodes", "createmasternode",   &createmasternode,   {"inputs", "metadata"}},
                {"masternodes", "resignmasternode",   &resignmasternode,   {"inputs", "mn_id"}},
                {"masternodes", "listmasternodes",    &listmasternodes,    {"pagination", "verbose"}},
                {"masternodes", "getmasternode",      &getmasternode,      {"mn_id"}},
                {"masternodes", "listcriminalproofs", &listcriminalproofs, {}},
                {"tokens",      "createtoken",        &createtoken,        {"inputs", "metadata"}},
                {"tokens",      "destroytoken",       &destroytoken,       {"inputs", "symbol"}},
                {"tokens",      "listtokens",         &listtokens,         {"pagination", "verbose"}},
                {"tokens",      "gettoken",           &gettoken,           {"key" }},
                {"tokens",      "minttokens",         &minttokens,         {"inputs", "symbol", "amounts"}},
                {"dex",         "createorder",        &createorder,        {"inputs", "metadata"}},
                {"dex",         "destroyorder",       &destroyorder,       {"inputs", "order_txid", "owner_address"}},
                {"dex",         "matchorders",        &matchorders,        {"inputs", "matcher", "alice", "carol"}},
                {"dex",         "listorders",         &listorders,         {"pagination", "verbose"}},
                {"dex",         "getorder",           &getorder,           {"txid"}},
                {"dex",         "matchordersinfo",    &matchordersinfo,    {"alice", "carol"}},
                {"accounts",    "listaccounts",       &listaccounts,       {"pagination", "verbose"}},
                {"accounts",    "getaccount",         &getaccount,         {"owner", "pagination"}},
                {"accounts",    "utxostoaccount",     &utxostoaccount,     {"inputs", "amounts"}},
                {"accounts",    "accounttoaccount",   &accounttoaccount,   {"inputs", "sender", "to"}},
                {"accounts",    "accounttoutxos",     &accounttoutxos,     {"inputs", "sender", "to"}},
        };

void RegisterMasternodesRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
