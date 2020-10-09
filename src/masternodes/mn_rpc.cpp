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
#include <wallet/ismine.h>
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

// returns either base58/bech32 address, or hex if format is unknown
std::string ScriptToString(CScript const& script) {
    CTxDestination dest;
    if (!ExtractDestination(script, dest)) {
        return script.GetHex();
    }
    return EncodeDestination(dest);
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
               "\nCreates (and submits to local node and network) a masternode creation transaction with given owner and operator addresses, spending the given inputs..\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Any valid address for keeping collateral amount (any P2PKH or P2WKH address) - used as owner key"},
                   {"operatorAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Optional (== ownerAddress) masternode operator auth address (P2PKH only, unique)"},
                   {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "A json array of json objects",
                       {
                           {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                               {
                                   {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                                   {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                               },
                           },
                       },
                   },
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                   HelpExampleCli("createmasternode", "ownerAddress operatorAddress \"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\"")
                   + HelpExampleRpc("createmasternode", "ownerAddress operatorAddress \"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\"")
               },
    }.Check(request);


    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create Masternode while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, { UniValue::VSTR, UniValue::VSTR, UniValue::VARR }, true);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, at least argument 1 must be non-null");
    }

    std::string ownerAddress = request.params[0].getValStr();
    std::string operatorAddress = request.params.size() > 1 ? request.params[1].getValStr() : ownerAddress;
    CTxDestination ownerDest = DecodeDestination(ownerAddress); // type will be checked on apply/create
    CTxDestination operatorDest = DecodeDestination(operatorAddress);

    // check type here cause need operatorAuthKey. all other validation (for owner for ex.) in further apply/create
    if (operatorDest.which() != 1 && operatorDest.which() != 4) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorAddress (" + operatorAddress + ") does not refer to a P2PKH or P2WPKH address");
    }

    CKeyID const operatorAuthKey = operatorDest.which() == 1 ? CKeyID(*boost::get<PKHash>(&operatorDest)) : CKeyID(*boost::get<WitnessV0KeyHash>(&operatorDest));

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateMasternode)
             << static_cast<char>(operatorDest.which()) << operatorAuthKey;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(::ChainActive().Height());
    CMutableTransaction rawTx(txVersion);

    if (request.params.size() > 2) {
        rawTx.vin = GetInputs(request.params[2].get_array());
    }

    rawTx.vout.push_back(CTxOut(EstimateMnCreationFee(), scriptMeta));
    rawTx.vout.push_back(CTxOut(GetMnCollateralAmount(), GetScriptForDestination(ownerDest)));

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
                                                    "The last optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the MN's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"mn_id", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The Masternode's ID"},
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
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                   HelpExampleCli("resignmasternode", "mn_id \"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\"")
                   + HelpExampleRpc("resignmasternode", "mn_id \"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\"")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot resign Masternode while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, { UniValue::VSTR, UniValue::VARR }, true);

    std::string const nodeIdStr = request.params[0].getValStr();
    uint256 nodeId = uint256S(nodeIdStr);
    CTxDestination ownerDest;

    {
        LOCK(cs_main);
        auto nodePtr = pcustomcsview->GetMasternode(nodeId);
        if (!nodePtr) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("The masternode %s does not exist", nodeIdStr));
        }
        ownerDest = nodePtr->ownerType == 1 ? CTxDestination(PKHash(nodePtr->ownerAuthAddress)) : CTxDestination(WitnessV0KeyHash(nodePtr->ownerAuthAddress));
    }

    const auto txVersion = GetTransactionVersion(::ChainActive().Height());
    CMutableTransaction rawTx(txVersion);
    rawTx.vin = GetAuthInputs(pwallet, ownerDest, request.params.size() > 1 ? request.params[1].get_array() : UniValue(UniValue::VARR));

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
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"symbol", RPCArg::Type::STR, RPCArg::Optional::NO,
                             "Token's symbol (unique), no longer than " +
                             std::to_string(CToken::MAX_TOKEN_SYMBOL_LENGTH)},
                            {"name", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                             "Token's name (optional), no longer than " +
                             std::to_string(CToken::MAX_TOKEN_NAME_LENGTH)},
                            {"isDAT", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                             "Token's 'isDAT' property (bool, optional), default is 'False'"},
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
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("createtoken", "\"{\\\"symbol\\\":\\\"MyToken\\\","
                                                     "\\\"collateralAddress\\\":\\\"address\\\"}\"")
                       + HelpExampleCli("createtoken", "\"{\\\"symbol\\\":\\\"MyToken\\\","
                                                     "\\\"collateralAddress\\\":\\\"address\\\"}\" "
                                                     "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\"")
                       + HelpExampleRpc("createtoken", "\"{\\\"symbol\\\":\\\"MyToken\\\","
                                                       "\\\"collateralAddress\\\":\\\"address\\\"}\" "
                                                       "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\"")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create token while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object at least with "
                           "{\"symbol\",\"collateralAddress\"}");
    }

    const UniValue metaObj = request.params[0].get_obj();
    UniValue txInputs = request.params[1];
    if (txInputs.isNull())
    {
        txInputs.setArray();
    }

    std::string collateralAddress = metaObj["collateralAddress"].getValStr();
    CTxDestination collateralDest = DecodeDestination(collateralAddress);
    if (collateralDest.which() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "collateralAddress (" + collateralAddress + ") does not refer to any valid address");
    }

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

    CToken token;
    token.symbol = trim_ws(metaObj["symbol"].getValStr()).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    token.name = trim_ws(metaObj["name"].getValStr()).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    token.flags = metaObj["isDAT"].getBool() ? token.flags | (uint8_t)CToken::TokenFlags::isDAT : token.flags; // setting isDAT
//    token.decimal = metaObj["name"].get_int(); // fixed for now, check range later
//    token.limit = metaObj["limit"].get_int(); // fixed for now, check range later
//    token.flags = metaObj["mintable"].get_bool() ? token.flags | CToken::TokenFlags::Mintable : token.flags; // fixed for now, check later
//    token.flags = metaObj["tradeable"].get_bool() ? token.flags | CToken::TokenFlags::Tradeable : token.flags; // fixed for now, check later

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateToken)
             << token;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    if(metaObj["isDAT"].getBool())
    {
        for(std::set<CScript>::iterator it = Params().GetConsensus().foundationMembers.begin(); it != Params().GetConsensus().foundationMembers.end() && rawTx.vin.size() == 0; it++)
        {
            if(IsMine(*pwallet, *it) == ISMINE_SPENDABLE)
            {
                CTxDestination destination;
                if (!ExtractDestination(*it, destination)) {
                    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid destination");
                }
                try {
                    rawTx.vin = GetAuthInputs(pwallet, destination, txInputs.get_array());
                }
                catch (const UniValue& objError) {}
            }
        }
        if(rawTx.vin.size() == 0)
            throw JSONRPCError(RPC_INVALID_REQUEST, "Incorrect Authorization");
    }
    else
        rawTx.vin = GetInputs(txInputs.get_array());

    rawTx.vout.push_back(CTxOut(GetTokenCreationFee(targetHeight), scriptMeta));
    rawTx.vout.push_back(CTxOut(GetTokenCollateralAmount(), GetScriptForDestination(collateralDest)));

    rawTx = fund(rawTx, request, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyCreateTokenTx(mnview_dummy, g_chainstate->CoinsTip(), CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, token}), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, request, pwallet)->GetHash().GetHex();
}

UniValue updatetoken(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"updatetoken",
               "\nCreates (and submits to local node and network) a transaction of token promotion to isDAT or demotion from isDAT. Collateral will be unlocked.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the token's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                           {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "The tokens's symbol, id or creation tx"},
                           {"isDAT", RPCArg::Type::BOOL, RPCArg::Optional::NO, "Token's 'isDAT' new property (bool)"},
                        },
                    },
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
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("updatetoken", "\"{\\\"token\\\":\\\"DFI\\\", \\\"isDAT\\\":true}\" "
                                                     "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\"")
                       + HelpExampleRpc("updatetoken", "\"{\\\"token\\\":\\\"DFI\\\", \\\"isDAT\\\":true}\" "
                                                       "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\"")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot update token while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);
    if (request.params[0].isNull()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Invalid parameters, arguments 1 must be non-null and expected as object like "
                           "{\"token\":\"Symbol\", \"isDAT\":true}");
    }

    UniValue metaObj = request.params[0].get_obj();
    UniValue txInputs = request.params[1];
    if (txInputs.isNull())
    {
        txInputs.setArray();
    }

    std::string const tokenStr = trim_ws(metaObj["token"].getValStr());
    CTxDestination ownerDest;
    uint256 creationTx{};
    int targetHeight;
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
        creationTx = tokenImpl.creationTx;
        targetHeight = ::ChainActive().Height() + 1;
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    for(std::set<CScript>::iterator it = Params().GetConsensus().foundationMembers.begin(); it != Params().GetConsensus().foundationMembers.end() && rawTx.vin.size() == 0; it++)
    {
        if(IsMine(*pwallet, *it) == ISMINE_SPENDABLE)
        {
            CTxDestination destination;
            if (!ExtractDestination(*it, destination)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid destination");
            }
            try {
                rawTx.vin = GetAuthInputs(pwallet, destination, txInputs.get_array());
            }
            catch (const UniValue& objError) {}
        }
    }
    if(rawTx.vin.size() == 0)
        throw JSONRPCError(RPC_INVALID_REQUEST, "Incorrect Authorization");

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::UpdateToken)
             << creationTx << metaObj["isDAT"].getBool();

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    rawTx = fund(rawTx, request, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyUpdateTokenTx(mnview_dummy, ::ChainstateActive().CoinsTip(), CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, creationTx, metaObj["isDAT"].getBool()}), Params().GetConsensus());
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
        tokenObj.pushKV("isDAT", token.IsDAT());
        if (id >= CTokensView::DCT_ID_START) {
            CTokenImplementation const& tokenImpl = static_cast<CTokenImplementation const&>(token);
            tokenObj.pushKV("minted", ValueFromAmount(tokenImpl.minted));
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
               "\nCreates (and submits to local node and network) a transaction minting your token (for accounts and/or UTXOs). \n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the token's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"amounts", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "Amount in amount@token format."
                    },
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
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("minttokens", "\"10@symbol\"")
                       + HelpExampleCli("minttokens",
                                      "\"10@symbol\" \"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\"")  /// @todo tokens: modify
                       + HelpExampleRpc("minttokens", "\"10@symbol\" \"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\"")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot mint tokens while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    const CBalances minted = DecodeAmounts(pwallet->chain(), request.params[0], "");
    UniValue txInputs = request.params[1];
    if (txInputs.isNull())
    {
        txInputs.setArray();
    }

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    // auth
    {
        for (auto const & kv : minted.balances) {
            if (kv.first < CTokensView::DCT_ID_START) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s is a 'stable coin', can't mint stable coin", kv.first.ToString()));
            }
            CTxDestination ownerDest;
            {
                LOCK(cs_main);
                auto token = pcustomcsview->GetToken(kv.first);
                if (!token) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", kv.first.ToString()));
                }

                auto tokenImpl = static_cast<CTokenImplementation const& >(*token);
                if (tokenImpl.destructionTx != uint256{}) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER,
                                       strprintf("Token %s already destroyed at height %i by tx %s", tokenImpl.symbol,
                                                 tokenImpl.destructionHeight, tokenImpl.destructionTx.GetHex()));
                }
                LOCK(pwallet->cs_wallet);
                auto wtx = pwallet->GetWalletTx(tokenImpl.creationTx);
                if (!wtx || !ExtractDestination(wtx->tx->vout[1].scriptPubKey, ownerDest)) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER,
                                       strprintf("Can't extract destination for token's %s collateral", tokenImpl.symbol));
                }
            }
            rawTx.vin = GetAuthInputs(pwallet, ownerDest, txInputs.get_array());
        }
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::MintToken)
             << minted; /// @note here, that whole CBalances serialized!, not a 'minted.balances'!

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    // fund
    rawTx = fund(rawTx, request, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyMintTokenTx(mnview_dummy, g_chainstate->CoinsTip(), CTransaction(rawTx), targetHeight,
                                                 ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, minted }), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }

    return signsend(rawTx, request, pwallet)->GetHash().GetHex();
}

CScript hexToScript(std::string const& str) {
    if (!IsHex(str)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "(" + str + ") doesn't represent a correct hex:\n");
    }
    const auto raw = ParseHex(str);
    return CScript{raw.begin(), raw.end()};
}

BalanceKey decodeBalanceKey(std::string const& str) {
    const auto pair = SplitAmount(str);
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

UniValue accountToJSON(CScript const& owner, CTokenAmount const& amount, bool verbose, bool indexed_amounts) {
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

    if (indexed_amounts) {
        UniValue amountObj(UniValue::VOBJ);
        amountObj.pushKV(amount.nTokenId.ToString(), ValueFromAmount(amount.nValue));
        obj.pushKV("amount", amountObj);
    }
    else {
        obj.pushKV("amount", amount.ToString());
    }

    return obj;
}

UniValue listaccounts(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

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
                                   "Flag for verbose list (default = true), otherwise limited objects are listed"},
                       {"indexed_amounts", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                        "Format of amounts output (default = false): (true: {tokenid:amount}, false: \"amount@tokenid\")"},
                       {"is_mine_only", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                        "Get balances about all accounts belonging to the wallet"},
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
    bool indexed_amounts = false;
    if (request.params.size() > 2) {
        indexed_amounts = request.params[2].get_bool();
    }
    bool isMineOnly = false;
    if (request.params.size() > 3) {
        isMineOnly = request.params[3].get_bool();
    }


    UniValue ret(UniValue::VARR);

    LOCK(cs_main);
    pcustomcsview->ForEachBalance([&](CScript const & owner, CTokenAmount const & balance) {
        if (isMineOnly) {
            if (IsMine(*pwallet, owner) == ISMINE_SPENDABLE)
                ret.push_back(accountToJSON(owner, balance, verbose, indexed_amounts));
        } else {
            ret.push_back(accountToJSON(owner, balance, verbose, indexed_amounts));
        }

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
                    {"indexed_amounts", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                        "Format of amounts output (default = false): (true: obj = {tokenid:amount,...}, false: array = [\"amount@tokenid\"...])"},
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
    bool indexed_amounts = false;
    if (request.params.size() > 2) {
        indexed_amounts = request.params[2].get_bool();
    }

    UniValue ret(UniValue::VARR);
    if (indexed_amounts) {
        ret.setObject();
    }

    LOCK(cs_main);
    pcustomcsview->ForEachBalance([&](CScript const & owner, CTokenAmount const & balance) {
        if (owner != reqOwner) {
            return false;
        }

        if (indexed_amounts)
            ret.pushKV(balance.nTokenId.ToString(), ValueFromAmount(balance.nValue));
        else
            ret.push_back(balance.ToString());

        limit--;
        return limit != 0;
    }, BalanceKey{reqOwner, start});
    return ret;
}

UniValue gettokenbalances(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"gettokenbalances",
               "\nReturns the balances of all accounts that belong to the wallet.\n",
               {
                    {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"start", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                 "Optional first key to iterate from, in lexicographical order."
                                 "Typically it's set to last tokenID from previous request."},
                            {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                 "If true, then iterate including starting position. False by default"},
                            {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                 "Maximum number of tokens to return, 100 by default"},
                        },
                    },
                    {"indexed_amounts", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                        "Format of amounts output (default = false): (true: obj = {tokenid:amount,...}, false: array = [\"amount@tokenid\"...])"},
                },
                RPCResult{
                       "{...}     (array) Json object with balances information\n"
                },
                RPCExamples{
                       HelpExampleCli("gettokenbalances", "")
                },
    }.Check(request);

    // parse pagination
    size_t limit = 100;
    DCT_ID start = {};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
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
    bool indexed_amounts = false;
    if (request.params.size() > 1) {
        indexed_amounts = request.params[1].get_bool();
    }

    UniValue ret(UniValue::VARR);
    if (indexed_amounts) {
        ret.setObject();
    }

    LOCK(cs_main);
    CBalances totalBalances;
    CScript oldOwner;
    pcustomcsview->ForEachBalance([&](CScript const & owner, CTokenAmount const & balance) {
        if (oldOwner == owner) {
            totalBalances.Add(balance);
        } else if (IsMine(*pwallet, owner) == ISMINE_SPENDABLE) {
            oldOwner = owner;
            totalBalances.Add(balance);
        }
        return true;
    }, BalanceKey{});
    auto it = totalBalances.balances.find(start);
    for (int i = 0; it != totalBalances.balances.end() && i < limit; it++, i++) {
        CTokenAmount bal = CTokenAmount{(*it).first, (*it).second};
        if (indexed_amounts)
                ret.pushKV(bal.nTokenId.ToString(), ValueFromAmount(bal.nValue));
            else
                ret.push_back(bal.ToString());
    }
    return ret;
}

UniValue utxostoaccount(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"utxostoaccount",
               "\nCreates (and submits to local node and network) a transfer transaction from the wallet UTXOs to specfied account.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"amounts", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address is the key, the value is amount in amount@token format. "
                                                                                 "If multiple tokens are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"},
                        },
                    },
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
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("utxostoaccount", "\"{\\\"address1\\\":\\\"1.0@DFI\\\","
                                                     "\\\"address2\\\":[\\\"2.0@BTC\\\", \\\"3.0@ETH\\\"]"
                                                     "}\" \"[]\"")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, false);

    // decode recipients
    CUtxosToAccountMessage msg{};
    msg.to = DecodeRecipients(pwallet->chain(), request.params[0].get_obj());

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

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

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
        const auto res = ApplyUtxosToAccountTx(mnview_dummy, CTransaction(rawTx), targetHeight,
                                               ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}), Params().GetConsensus());
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
                    {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address of sender"},
                    {"to", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address is the key, the value is amount in amount@token format. "
                                                                                     "If multiple tokens are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"},
                        },
                    },
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
                },
                RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
                },
                RPCExamples{
                       HelpExampleCli("accounttoaccount", "sender_address "
                                                     "\"{\\\"address1\\\":\\\"1.0@DFI\\\",\\\"address2\\\":[\\\"2.0@BTC\\\", \\\"3.0@ETH\\\"]}\" "
                                                     "[]")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ, UniValue::VARR}, false);

    // decode sender and recipients
    CAccountToAccountMessage msg{};
    msg.from = DecodeScript(request.params[0].get_str());
    msg.to = DecodeRecipients(pwallet->chain(), request.params[1].get_obj());
    if (SumAllTransfers(msg.to).balances.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "zero amounts");
    }

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::AccountToAccount)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));
    CTxDestination ownerDest;
    if (!ExtractDestination(msg.from, ownerDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid owner destination");
    }

    UniValue txInputs = request.params[2];
    if (txInputs.isNull())
    {
        txInputs.setArray();
    }
    rawTx.vin = GetAuthInputs(pwallet, ownerDest, txInputs.get_array());

    // fund
    rawTx = fund(rawTx, request, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyAccountToAccountTx(mnview_dummy, g_chainstate->CoinsTip(), CTransaction(rawTx), targetHeight,
                                               ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}), Params().GetConsensus());
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
               "\nCreates (and submits to local node and network) a transfer transaction from the specified account to UTXOs.\n"
               "The third optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address of sender"},
                    {"to", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                            {"address", RPCArg::Type::STR, RPCArg::Optional::NO,
                                 "The defi address is the key, the value is amount in amount@token format. "
                                 "If multiple tokens are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"
                            },
                        },
                    },
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
                },
                RPCResult{
                       "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
                },
               RPCExamples{
                       HelpExampleCli("accounttoutxos", "sender_address \\\"{\\\"address1\\\":\\\"100@DFI\"}\\\" [] ")
                       + HelpExampleCli("accounttoutxos", "sender_address \"{\\\"address1\\\":\\\"1.0@DFI\\\",\\\"address2\\\":[\\\"2.0@BTC\\\", \\\"3.0@ETH\\\"]}\" []")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ, UniValue::VARR}, false);

    // decode sender and recipients
    CAccountToUtxosMessage msg{};
    msg.from = DecodeScript(request.params[0].get_str());
    const auto to = DecodeRecipients(pwallet->chain(), request.params[1]);
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

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

    // auth
    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    
    CTxDestination ownerDest;
    if (!ExtractDestination(msg.from, ownerDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid owner destination");
    }

    UniValue txInputs = request.params[2];
    if (txInputs.isNull())
    {
        txInputs.setArray();
    }
    rawTx.vin = GetAuthInputs(pwallet, ownerDest, txInputs.get_array());

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
        const auto res = ApplyAccountToUtxosTx(mnview_dummy, g_chainstate->CoinsTip(), CTransaction(rawTx), targetHeight,
                                                 ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}), Params().GetConsensus());
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

UniValue listcommunitybalances(const JSONRPCRequest& request) {
    RPCHelpMan{"listcommunitybalances",
               "\nReturns information about all community balances.\n",
               {
               },
               RPCResult{
                       "{balance_type:value,...}     (array) Json object with accounts information\n"
               },
               RPCExamples{
                       HelpExampleCli("listcommunitybalances", "")
                       + HelpExampleRpc("listcommunitybalances", "")
               },
    }.Check(request);

    /// @todo is it important to restrict with `Params().GetConsensus().AMKHeight` ??

    UniValue ret(UniValue::VOBJ);

    LOCK(cs_main);
    for (auto kv : Params().GetConsensus().nonUtxoBlockSubsidies) {
        ret.pushKV(GetCommunityAccountName(kv.first), ValueFromAmount(pcustomcsview->GetCommunityBalance(kv.first)));
    }

    return ret;
}


static const CRPCCommand commands[] =
{ //  category      name                  actor (function)     params
  //  ----------------- ------------------------    -----------------------     ----------
    {"masternodes", "createmasternode",   &createmasternode,   {"ownerAddress", "operatorAddress", "inputs"}},
    {"masternodes", "resignmasternode",   &resignmasternode,   {"mn_id", "inputs"}},
    {"masternodes", "listmasternodes",    &listmasternodes,    {"pagination", "verbose"}},
    {"masternodes", "getmasternode",      &getmasternode,      {"mn_id"}},
    {"masternodes", "listcriminalproofs", &listcriminalproofs, {}},
    {"tokens",      "createtoken",        &createtoken,        {"metadata", "inputs"}},
    {"tokens",      "updatetoken",        &updatetoken,        {"metadata", "inputs"}},
    {"tokens",      "listtokens",         &listtokens,         {"pagination", "verbose"}},
    {"tokens",      "gettoken",           &gettoken,           {"key" }},
    {"tokens",      "minttokens",         &minttokens,         {"amounts", "inputs"}},
    {"accounts",    "listaccounts",       &listaccounts,       {"pagination", "verbose", "indexed_amounts", "is_mine_only"}},
    {"accounts",    "getaccount",         &getaccount,         {"owner", "pagination", "indexed_amounts"}},
    {"accounts",    "gettokenbalances",   &gettokenbalances,   {"pagination", "indexed_amounts"}},
    {"accounts",    "utxostoaccount",     &utxostoaccount,     {"amounts", "inputs"}},
    {"accounts",    "accounttoaccount",   &accounttoaccount,   {"from", "to", "inputs"}},
    {"accounts",    "accounttoutxos",     &accounttoutxos,     {"from", "to", "inputs"}},
    {"accounts",    "listcommunitybalances", &listcommunitybalances, {}},
};

void RegisterMasternodesRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
