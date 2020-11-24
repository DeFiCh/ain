// Copyright (c) 2020 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/masternodes.h>
#include <masternodes/criminals.h>
#include <masternodes/mn_checks.h>

#include <arith_uint256.h>
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

extern bool EnsureWalletIsAvailable(bool avoidException); // in rpcwallet.cpp
extern bool DecodeHexTx(CTransaction& tx, std::string const& strHexTx); // in core_io.h

extern void FundTransaction(CWallet* const pwallet, CMutableTransaction& tx, CAmount& fee_out, int& change_position,
                            UniValue options);

static CMutableTransaction fund(CMutableTransaction _mtx, CWallet* const pwallet) {
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
signsend(const CMutableTransaction& _mtx, JSONRPCRequest const& request) {
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

static int chainHeight(interfaces::Chain::Lock& locked_chain)
{
    if (auto height = locked_chain.getHeight())
        return *height;
    return 0;
}

// returns either base58/bech32 address, or hex if format is unknown
std::string ScriptToString(CScript const& script) {
    CTxDestination dest;
    if (!ExtractDestination(script, dest)) {
        return script.GetHex();
    }
    return EncodeDestination(dest);
}

CAmount EstimateMnCreationFee(int targetHeight) {
    // Current height + (1 day blocks) to avoid rejection;
    targetHeight += (60 * 60 / Params().GetConsensus().pos.nTargetSpacing);
    return GetMnCreationFee(targetHeight);
}

static std::vector<CTxIn> ParseInputs(UniValue const& inputs) {
    std::vector<CTxIn> vin{};
    auto& array = inputs.get_array();
    for (unsigned int idx = 0; idx < array.size(); idx++) {
        const UniValue& input = array[idx];
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

static bool HasAuthInputs(CWallet* const pwallet, CTxDestination const& auth, std::vector<CTxIn>& vin)
{
    CCoinControl cctl;
    cctl.m_min_depth = 1;
    cctl.m_max_depth = 999999999;
    cctl.matchDestination = auth;
    cctl.m_tokenFilter = {DCT_ID{0}};
    cctl.m_avoid_address_reuse = false;

    pwallet->BlockUntilSyncedToCurrentChain();
    auto locked_chain = pwallet->chain().lock();
    LOCK(pwallet->cs_wallet);

    std::vector<COutput> vecOutputs;
    pwallet->AvailableCoins(*locked_chain, vecOutputs, true, &cctl, 1, INT64_MAX, INT64_MAX, 1);
    if (vecOutputs.empty())
        return false;
    vin.emplace_back(vecOutputs[0].tx->GetHash(), uint32_t(vecOutputs[0].i));
    return true;
}

static std::vector<CTxIn> GetAuthInputs(CWallet* const pwallet, CTxDestination const& auth) {
    std::vector<CTxIn> vin;

    if (!HasAuthInputs(pwallet, auth, vin)) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strprintf(
                "Can't find any UTXO's for owner. Are you an owner? If so, send some coins to address %s and try again!",
                EncodeDestination(auth)));
    }
    return vin;
}

static CWallet* GetWallet(const JSONRPCRequest& request) {
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    EnsureWalletIsAvailable(pwallet, request.fHelp);
    return pwallet;
}

static std::vector<CTxIn> GetMineAuthInputs(CWallet* const pwallet)
{
    CTxDestination destination;
    for(auto& script : Params().GetConsensus().foundationMembers) {
        if(IsMine(*pwallet, script) == ISMINE_SPENDABLE) {
            if (!ExtractDestination(script, destination)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid destination");
            }
            std::vector<CTxIn> vin;
            if (HasAuthInputs(pwallet, destination, vin))
                return vin;
        }
    }
    return {};
}

inline bool isNonEmptyArray(const UniValue& value)
{
    return value.isArray() && !value.empty();
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
                   HelpExampleCli("createmasternode", "ownerAddress operatorAddress '[{\"txid\":\"id\",\"vout\":0}]'")
                   + HelpExampleRpc("createmasternode", "ownerAddress operatorAddress '[{\"txid\":\"id\",\"vout\":0}]'")
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

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    const auto& txInputs = request.params[2];
    if (isNonEmptyArray(txInputs)) {
        rawTx.vin = ParseInputs(txInputs);
    }

    rawTx.vout.push_back(CTxOut(EstimateMnCreationFee(targetHeight), scriptMeta));
    rawTx.vout.push_back(CTxOut(GetMnCollateralAmount(), GetScriptForDestination(ownerDest)));

    rawTx = fund(rawTx, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyCreateMasternodeTx(mnview_dummy, CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, static_cast<char>(operatorDest.which()), operatorAuthKey}));
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, request)->GetHash().GetHex();
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
                   HelpExampleCli("resignmasternode", "mn_id '[{\"txid\":\"id\",\"vout\":0}]'")
                   + HelpExampleRpc("resignmasternode", "mn_id '[{\"txid\":\"id\",\"vout\":0}]'")
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
    int targetHeight;
    {
        LOCK(cs_main);
        auto nodePtr = pcustomcsview->GetMasternode(nodeId);
        if (!nodePtr) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("The masternode %s does not exist", nodeIdStr));
        }
        ownerDest = nodePtr->ownerType == 1 ? CTxDestination(PKHash(nodePtr->ownerAuthAddress)) : CTxDestination(WitnessV0KeyHash(nodePtr->ownerAuthAddress));
        targetHeight = ::ChainActive().Height() + 1;
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    const auto& txInputs = request.params[1];
    rawTx.vin = isNonEmptyArray(txInputs) ? ParseInputs(txInputs)
                                          : GetAuthInputs(pwallet, ownerDest);

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::ResignMasternode)
             << nodeId;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    rawTx = fund(rawTx, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyResignMasternodeTx(mnview_dummy, ::ChainstateActive().CoinsTip(), CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, nodeId}));
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, request)->GetHash().GetHex();
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
                       HelpExampleCli("listmasternodes", "'[mn_id]' False")
                       + HelpExampleRpc("listmasternodes", "'[mn_id]' False")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].get_bool();
    }
    // parse pagination
    size_t limit = 100;
    uint256 start = {};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
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
                       HelpExampleCli("getmasternode", "mn_id")
                       + HelpExampleRpc("getmasternode", "mn_id")
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
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
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
                             "Token's decimal places (optional, default is 8)"},
                            {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                             "Token's total supply limit (optional)"},
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
                       HelpExampleCli("createtoken", "'{\"symbol\":\"MyToken\","
                                                     "\"collateralAddress\":\"address\"}'")
                       + HelpExampleCli("createtoken", "'{\"symbol\":\"MyToken\","
                                                     "\"collateralAddress\":\"address\"}' "
                                                     "'[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("createtoken", "'{\"symbol\":\"MyToken\","
                                                       "\"collateralAddress\":\"address\"}' "
                                                       "'[{\"txid\":\"id\",\"vout\":0}]'")
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
    UniValue const& txInputs = request.params[1];

    std::string collateralAddress = metaObj["collateralAddress"].getValStr();
    CTxDestination collateralDest = DecodeDestination(collateralAddress);
    if (collateralDest.which() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "collateralAddress (" + collateralAddress + ") does not refer to any valid address");
    }

    CToken token;
    token.symbol = trim_ws(metaObj["symbol"].getValStr()).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    token.name = trim_ws(metaObj["name"].getValStr()).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);

    if (!metaObj["isDAT"].isNull()) {
        token.flags = metaObj["isDAT"].getBool() ?
                        token.flags | (uint8_t)CToken::TokenFlags::DAT :
                        token.flags & ~(uint8_t)CToken::TokenFlags::DAT;
    }
    if (!metaObj["tradeable"].isNull()) {
        token.flags = metaObj["tradeable"].getBool() ?
                        token.flags | (uint8_t)CToken::TokenFlags::Tradeable :
                        token.flags & ~(uint8_t)CToken::TokenFlags::Tradeable;
    }
    if (!metaObj["mintable"].isNull()) {
        token.flags = metaObj["mintable"].getBool() ?
                        token.flags | (uint8_t)CToken::TokenFlags::Mintable :
                        token.flags & ~(uint8_t)CToken::TokenFlags::Mintable;
    }

    if (!metaObj["decimal"].isNull()) {
        auto decimal = metaObj["decimal"].get_int();
        if (decimal > DECIMAL_LIMIT)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "coin value is too small");
        token.decimal = uint8_t(decimal);
    }

    if (!metaObj["limit"].isNull()) {
        auto limit = metaObj["limit"].get_int64();
        if (limit < MIN_COINS_VALUE)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "token should have at least " + std::to_string(MIN_COINS_VALUE) + " coins");
        auto diff = std::numeric_limits<int64_t>::max() / limit;
        auto precision = std::pow(10, token.decimal);
        if (precision > diff)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "limit exceed int64");
        token.limit = limit * precision;
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreateToken)
             << token;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    if (isNonEmptyArray(txInputs)) {
        rawTx.vin = ParseInputs(txInputs);
    } else if(metaObj["isDAT"].getBool()) {
        rawTx.vin = GetMineAuthInputs(pwallet);
        if(rawTx.vin.empty())
            throw JSONRPCError(RPC_INVALID_REQUEST, "Incorrect Authorization");
    }

    rawTx.vout.push_back(CTxOut(GetTokenCreationFee(targetHeight), scriptMeta));
    rawTx.vout.push_back(CTxOut(GetTokenCollateralAmount(), GetScriptForDestination(collateralDest)));

    rawTx = fund(rawTx, pwallet);

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
    return signsend(rawTx, request)->GetHash().GetHex();
}

UniValue updatetoken(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"updatetoken",
               "\nCreates (and submits to local node and network) a transaction of token promotion to isDAT or demotion from isDAT. Collateral will be unlocked.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the token's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                    {"token", RPCArg::Type::STR, RPCArg::Optional::NO, "The tokens's symbol, id or creation tx"},
                    {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                           {"symbol", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                            "New token's symbol, no longer than " +
                            std::to_string(CToken::MAX_TOKEN_SYMBOL_LENGTH)},
                           {"name", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                            "New token's name (optional), no longer than " +
                            std::to_string(CToken::MAX_TOKEN_NAME_LENGTH)},
                           {"isDAT", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                            "Token's 'isDAT' property (bool, optional), default is 'False'"},
                           {"mintable", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                            "Token's 'Mintable' property (bool, optional)"},
                           {"tradeable", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                            "Token's 'Tradeable' property (bool, optional)"},
                           {"finalize", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                            "Lock token properties forever (bool, optional)"},
                           // it is possible to transfer token's owner. but later
//                           {"collateralAddress", RPCArg::Type::STR, RPCArg::Optional::NO,
//                            "Any valid destination for keeping collateral amount - used as token's owner auth"},
                           // omitted for now, need to research/discuss
//                           {"decimal", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
//                            "Token's decimal places (optional, fixed to 8 for now, unchecked)"},
//                           {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
//                            "Token's total supply limit (optional, zero for now, unchecked)"},
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
                       HelpExampleCli("updatetoken", "token '{\"isDAT\":true}' "
                                                     "'[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("updatetoken", "token '{\"isDAT\":true}' "
                                                       "'[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot update token while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VOBJ, UniValue::VARR}, true);

    /// @todo RPCTypeCheckObj or smth to help with option's names and old/new tx type

    std::string const tokenStr = trim_ws(request.params[0].getValStr());
    UniValue const& metaObj = request.params[1].get_obj();
    UniValue const& txInputs = request.params[2];

    CTokenImplementation tokenImpl;
    CTxDestination ownerDest;
    CScript owner;
    int targetHeight;
    {
        LOCK(cs_main);
        DCT_ID id;
        auto token = pcustomcsview->GetTokenGuessId(tokenStr, id);
        if (id == DCT_ID{0}) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Can't alter DFI token!"));
        }
        if (!token) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", tokenStr));
        }
        tokenImpl = static_cast<CTokenImplementation const& >(*token);
        if (tokenImpl.IsPoolShare()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s is the LPS token! Can't alter pool share's tokens!", tokenStr));
        }

        const Coin& authCoin = ::ChainstateActive().CoinsTip().AccessCoin(COutPoint(tokenImpl.creationTx, 1)); // always n=1 output
        if (!ExtractDestination(authCoin.out.scriptPubKey, ownerDest)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               strprintf("Can't extract destination for token's %s collateral", tokenImpl.symbol));
        }
        owner = authCoin.out.scriptPubKey;
        targetHeight = ::ChainActive().Height() + 1;
    }

    if (!metaObj["symbol"].isNull()) {
        tokenImpl.symbol = trim_ws(metaObj["symbol"].getValStr()).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    }
    if (!metaObj["name"].isNull()) {
        tokenImpl.name = trim_ws(metaObj["name"].getValStr()).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    }
    if (!metaObj["isDAT"].isNull()) {
        tokenImpl.flags = metaObj["isDAT"].getBool() ?
                              tokenImpl.flags | (uint8_t)CToken::TokenFlags::DAT :
                              tokenImpl.flags & ~(uint8_t)CToken::TokenFlags::DAT;
    }
    if (!metaObj["tradeable"].isNull()) {
        tokenImpl.flags = metaObj["tradeable"].getBool() ?
                              tokenImpl.flags | (uint8_t)CToken::TokenFlags::Tradeable :
                              tokenImpl.flags & ~(uint8_t)CToken::TokenFlags::Tradeable;
    }
    if (!metaObj["mintable"].isNull()) {
        tokenImpl.flags = metaObj["mintable"].getBool() ?
                              tokenImpl.flags | (uint8_t)CToken::TokenFlags::Mintable :
                              tokenImpl.flags & ~(uint8_t)CToken::TokenFlags::Mintable;
    }
    if (!metaObj["finalize"].isNull()) {
        tokenImpl.flags = metaObj["finalize"].getBool() ?
                              tokenImpl.flags | (uint8_t)CToken::TokenFlags::Finalized :
                              tokenImpl.flags;
    }

    /// @todo replace all heights with (+1)
    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    bool searchInFoundationMemebers = true;

    if (targetHeight < Params().GetConsensus().BayfrontHeight) {
        if (metaObj.size() > 1 || !metaObj.exists("isDAT")) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Only 'isDAT' flag modification allowed before Bayfront fork (<" + std::to_string(Params().GetConsensus().BayfrontHeight) + ")");
        }
    } else if (isNonEmptyArray(txInputs)) { // post-bayfront auth
        rawTx.vin = ParseInputs(txInputs);
        searchInFoundationMemebers = false;
    } else if (!Params().GetConsensus().foundationMembers.count(owner)) {
        rawTx.vin = GetAuthInputs(pwallet, ownerDest);
        searchInFoundationMemebers = false;
    }

    if (searchInFoundationMemebers)
        rawTx.vin = GetMineAuthInputs(pwallet);

    if(rawTx.vin.empty())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Incorrect Authorization");

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);

    // tx type and serialized data differ:
    if (targetHeight < Params().GetConsensus().BayfrontHeight) {
        metadata << static_cast<unsigned char>(CustomTxType::UpdateToken)
                 << tokenImpl.creationTx <<  metaObj["isDAT"].getBool();
    }
    else {
        metadata << static_cast<unsigned char>(CustomTxType::UpdateTokenAny)
                 << tokenImpl.creationTx << static_cast<CToken>(tokenImpl); // casting to base token's data
    }

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    rawTx = fund(rawTx, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        Res res{};
        if (targetHeight < Params().GetConsensus().BayfrontHeight) {
            res = ApplyUpdateTokenTx(mnview_dummy, ::ChainstateActive().CoinsTip(), CTransaction(rawTx), targetHeight,
                                          ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, tokenImpl.creationTx, metaObj["isDAT"].getBool()}), Params().GetConsensus());
        }
        else {
            res = ApplyUpdateTokenAnyTx(mnview_dummy, ::ChainstateActive().CoinsTip(), CTransaction(rawTx), targetHeight,
                                          ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, tokenImpl.creationTx, static_cast<CToken>(tokenImpl)}), Params().GetConsensus());
        }
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, request)->GetHash().GetHex();
}

UniValue tokenToJSON(DCT_ID const& id, CTokenImplementation const& token, bool verbose) {
    UniValue tokenObj(UniValue::VOBJ);
    tokenObj.pushKV("symbol", token.symbol);
    tokenObj.pushKV("symbolKey", token.CreateSymbolKey(id));

    tokenObj.pushKV("name", token.name);
    if (verbose) {
        tokenObj.pushKV("decimal", token.decimal);
        tokenObj.pushKV("limit", token.limit);
        tokenObj.pushKV("mintable", token.IsMintable());
        tokenObj.pushKV("tradeable", token.IsTradeable());
        tokenObj.pushKV("isDAT", token.IsDAT());
        tokenObj.pushKV("isLPS", token.IsPoolShare());
        tokenObj.pushKV("finalized", token.IsFinalized());

        tokenObj.pushKV("minted", ValueFromAmount(token.minted));
        tokenObj.pushKV("creationTx", token.creationTx.ToString());
        tokenObj.pushKV("creationHeight", token.creationHeight);
        tokenObj.pushKV("destructionTx", token.destructionTx.ToString());
        tokenObj.pushKV("destructionHeight", token.destructionHeight);
        if (!token.IsPoolShare()) {
            const Coin& authCoin = ::ChainstateActive().CoinsTip().AccessCoin(COutPoint(token.creationTx, 1)); // always n=1 output
            tokenObj.pushKV("collateralAddress", ScriptToString(authCoin.out.scriptPubKey));
        } else {
            tokenObj.pushKV("collateralAddress", "undefined");
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
                       HelpExampleCli("listtokens", "'{\"start\":128}' False")
                       + HelpExampleRpc("listtokens", "'{\"start\":128}' False")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].get_bool();
    }

    // parse pagination
    size_t limit = 100;
    DCT_ID start{0};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
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
    pcustomcsview->ForEachToken([&](DCT_ID const& id, CTokenImplementation const& token) {
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
        return tokenToJSON(id, *static_cast<CTokenImplementation*>(token.get()), true);
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
                       HelpExampleCli("minttokens", "10@symbol")
                       + HelpExampleCli("minttokens",
                                      "10@symbol '[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("minttokens", "10@symbol '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot mint tokens while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    const CBalances minted = DecodeAmounts(pwallet->chain(), request.params[0], "");
    const auto& txInputs = request.params[1];
    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    // auth
    if (isNonEmptyArray(txInputs)) {
        rawTx.vin = ParseInputs(txInputs);
    } else {
        bool gotFoundersAuth = false;
        for (auto const & kv : minted.balances) {
            CTokenImplementation tokenImpl;
            CTxDestination ownerDest;
            {
                LOCK(cs_main);
                auto token = pcustomcsview->GetToken(kv.first);
                if (!token) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Token %s does not exist!", kv.first.ToString()));
                }

                tokenImpl = static_cast<CTokenImplementation const& >(*token);
                const Coin& authCoin = ::ChainstateActive().CoinsTip().AccessCoin(COutPoint(tokenImpl.creationTx, 1)); // always n=1 output
                if (!ExtractDestination(authCoin.out.scriptPubKey, ownerDest)) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER,
                                        strprintf("Can't extract destination for token's %s collateral", tokenImpl.symbol));
                }
            }

            /// @note that there is no need to handle BayfrontHeight here cause (in the worst case) it'll be declined at Apply*; the rest parts are compatible

            // use different auth for DAT|nonDAT tokens:
            // first, try to auth by exect owner
            auto auths = GetAuthInputs(pwallet, ownerDest);
            if(tokenImpl.IsDAT() && auths.size() == 0 && !gotFoundersAuth) { // try "founders auth" only if "common" fails:
                auths = GetMineAuthInputs(pwallet);
                if(auths.size() == 0)
                    throw JSONRPCError(RPC_INVALID_REQUEST, "Incorrect Authorization");
                gotFoundersAuth = true; // we need only one any utxo from founder

            }
            for (const auto& auth : auths)
                if (std::find(rawTx.vin.begin(), rawTx.vin.end(), auth) == rawTx.vin.end())
                    rawTx.vin.push_back(auth);
        }
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::MintToken)
             << minted; /// @note here, that whole CBalances serialized!, not a 'minted.balances'!

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    // fund
    rawTx = fund(rawTx, pwallet);

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

    return signsend(rawTx, request)->GetHash().GetHex();
}

CScript hexToScript(std::string const& str) {
    if (!IsHex(str)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "(" + str + ") doesn't represent a correct hex:\n");
    }
    const auto raw = ParseHex(str);
    return CScript{raw.begin(), raw.end()};
}

BalanceKey decodeBalanceKey(std::string const& str) {
    DCT_ID tokenID{};
    auto symbol = str.find('@');
    if (symbol != str.npos) {
        auto id = DCT_ID::FromString(str.substr(symbol + 1));
        if (!id.ok) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "(" + str + ") doesn't represent a correct balance key:\n" + id.msg);
        }
        tokenID = *id.val;
    }
    return {hexToScript(str.substr(symbol)), tokenID};
}

UniValue accountToJSON(CScript const& owner, CTokenAmount const& amount, CToken const& token, bool verbose, bool indexed_amounts) {
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
        amountObj.pushKV(amount.nTokenId.ToString(), ValueFromAmount(amount.nValue, token.decimal));
        obj.pushKV("amount", amountObj);
    }
    else {
        obj.pushKV("amount", ValueFromAmount(amount.nValue, amount.nTokenId, token));
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
                       + HelpExampleRpc("listaccounts", "'{}' False")
                       + HelpExampleRpc("listaccounts", "'{\"start\":\"a914b12ecde1759f792e0228e4fa6d262902687ca7eb87@0\","
                                                      "\"limit\":1000"
                                                      "}'")
               },
    }.Check(request);

    // parse pagination
    size_t limit = 100;
    BalanceKey start = {};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
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
        auto token = pcustomcsview->GetToken(balance.nTokenId);
        assert(token);
        if (isMineOnly) {
            if (IsMine(*pwallet, owner) == ISMINE_SPENDABLE)
                ret.push_back(accountToJSON(owner, balance, *token, verbose, indexed_amounts));
        } else {
            ret.push_back(accountToJSON(owner, balance, *token, verbose, indexed_amounts));
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
    bool including_start = true;
    {
        if (request.params.size() > 1) {
            UniValue paginationObj = request.params[1].get_obj();
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

        auto token = pcustomcsview->GetToken(balance.nTokenId);
        assert(token);
        if (indexed_amounts)
            ret.pushKV(balance.nTokenId.ToString(), ValueFromAmount(balance.nValue, token->decimal));
        else
            ret.push_back(ValueFromAmount(balance.nValue, balance.nTokenId, *token));

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
                    {"symbol_lookup", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                        "Use token symbols in output (default = false)"},
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
        indexed_amounts = request.params[1].getBool();
    }
    bool symbol_lookup = false;
    if (request.params.size() > 2) {
        symbol_lookup = request.params[2].getBool();
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
    });
    auto it = totalBalances.balances.lower_bound(start);
    for (int i = 0; it != totalBalances.balances.end() && i < limit; it++, i++) {
        CTokenAmount bal = CTokenAmount{(*it).first, (*it).second};
        std::string tokenIdStr = bal.nTokenId.ToString();
        auto token = pcustomcsview->GetToken(bal.nTokenId);
        assert(token);
        if (symbol_lookup) {
            tokenIdStr = token->CreateSymbolKey(bal.nTokenId);
        }
        if (indexed_amounts)
            ret.pushKV(tokenIdStr, ValueFromAmount(bal.nValue, token->decimal));
        else
            ret.push_back(ValueFromAmount(bal.nValue, token->decimal).getValStr() + "@" + tokenIdStr);
    }
    return ret;
}

UniValue poolToJSON(DCT_ID const& id, CPoolPair const& pool, CToken const& token, bool verbose) {
    UniValue poolObj(UniValue::VOBJ);
    poolObj.pushKV("symbol", token.symbol);
    poolObj.pushKV("name", token.name);
    poolObj.pushKV("status", pool.status);
    poolObj.pushKV("idTokenA", pool.idTokenA.ToString());
    poolObj.pushKV("idTokenB", pool.idTokenB.ToString());

    if (verbose) {
        poolObj.pushKV("reserveA", ValueFromAmount(pool.reserveA, pool.tokenA.decimal));
        poolObj.pushKV("reserveB", ValueFromAmount(pool.reserveB, pool.tokenB.decimal));
        poolObj.pushKV("commission", ValueFromAmount(pool.commission));
        poolObj.pushKV("totalLiquidity", ValueFromAmount(pool.totalLiquidity, token.decimal));

        const auto decMax = std::max(pool.tokenA.decimal, pool.tokenB.decimal);
        const CAmount coin = std::pow(10, decMax);
        const CAmount decA = std::pow(10, decMax - pool.tokenA.decimal);
        const CAmount decB = std::pow(10, decMax - pool.tokenB.decimal);

        if (pool.reserveB == 0) {
            poolObj.pushKV("reserveA/reserveB", "0");
        } else {
            poolObj.pushKV("reserveA/reserveB", ValueFromAmount((arith_uint256(pool.reserveA) * decA * coin / pool.reserveB / decB).GetLow64()));
        }

        if (pool.reserveA == 0) {
            poolObj.pushKV("reserveB/reserveA", "0");
        } else {
            poolObj.pushKV("reserveB/reserveA", ValueFromAmount((arith_uint256(pool.reserveB) * decB * coin / pool.reserveA / decA).GetLow64()));
        }

        const CAmount coinA = std::pow(10, pool.tokenA.decimal);
        const CAmount swapRateA = CPoolPair::SLOPE_SWAP_RATE * COIN / coinA;
        const CAmount coinB = std::pow(10, pool.tokenB.decimal);
        const CAmount swapRateB = CPoolPair::SLOPE_SWAP_RATE * COIN / coinB;

        poolObj.pushKV("tradeEnabled", pool.reserveA >= swapRateA && pool.reserveB >= swapRateB);

        poolObj.pushKV("ownerAddress", pool.ownerAddress.GetHex()); /// @todo replace with ScriptPubKeyToUniv()

        poolObj.pushKV("blockCommissionA", ValueFromAmount(pool.blockCommissionA, pool.tokenA.decimal));
        poolObj.pushKV("blockCommissionB", ValueFromAmount(pool.blockCommissionB, pool.tokenB.decimal));

        poolObj.pushKV("rewardPct", ValueFromAmount(pool.rewardPct));

        poolObj.pushKV("creationTx", pool.creationTx.GetHex());
        poolObj.pushKV("creationHeight", (uint64_t) pool.creationHeight);
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(id.ToString(), poolObj);
    return ret;
}

UniValue listpoolpairs(const JSONRPCRequest& request) {
    RPCHelpMan{"listpoolpairs",
               "\nReturns information about pools.\n",
               {
                        {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                            {
                                 {"start", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Optional first key to iterate from, in lexicographical order."
                                  "Typically it's set to last ID from previous request."},
                                 {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                  "If true, then iterate including starting position. False by default"},
                                 {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Maximum number of pools to return, 100 by default"},
                            },
                        },
                        {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                    "Flag for verbose list (default = true), otherwise only ids, symbols and names are listed"},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with pools information\n"
               },
               RPCExamples{
                       HelpExampleCli("listpoolpairs", "'{\"start\":128}' False")
                       + HelpExampleRpc("listpoolpairs", "'{\"start\":128}' False")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].get_bool();
    }

    // parse pagination
    size_t limit = 100;
    DCT_ID start{0};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
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
    pcustomcsview->ForEachPoolPair([&](DCT_ID const & id, CPoolPair const & pool) {
        if (const auto token = pcustomcsview->GetToken(id))
            ret.pushKVs(poolToJSON(id, pool, *token, verbose));

        limit--;
        return limit != 0;
    }, start);

    return ret;
}

UniValue getpoolpair(const JSONRPCRequest& request) {
    RPCHelpMan{"getpoolpair",
               "\nReturns information about pool.\n",
               {
                       {"key", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "One of the keys may be specified (id/symbol/creationTx)"},
                       {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                        "Flag for verbose list (default = true), otherwise limited objects are listed"},
               },
               RPCResult{
                       "{id:{...}}     (array) Json object with pool information\n"
               },
               RPCExamples{
                       HelpExampleCli("getpoolpair", "GOLD")
                       + HelpExampleRpc("getpoolpair", "GOLD")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].getBool();
    }

    LOCK(cs_main);

    DCT_ID id;
    auto token = pcustomcsview->GetTokenGuessId(request.params[0].getValStr(), id);
    if (token) {
        if (auto pool = pcustomcsview->GetPoolPair(id)) {
            return poolToJSON(id, *pool, *token, verbose);
        }
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pool not found");
    }
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pool not found");
}

UniValue addpoolliquidity(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"addpoolliquidity",
               "\nCreates (and submits to local node and network) a add pool liquidity transaction.\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"from", RPCArg::Type::OBJ, RPCArg::Optional::NO, "",
                        {
                                {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address(es) is the key(s), the value(s) is amount in amount@token format. "
                                                                                     "You should provide exectly two types of tokens for pool's 'token A' and 'token B' in any combinations."
                                                                                     "If multiple tokens from one address are to be transferred, specify an array [\"amount1@t1\", \"amount2@t2\"]"},
                        },
                       },
                       {"shareAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address for crediting tokens."},
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
                       HelpExampleCli("addpoolliquidity",
                                      "'{\"address1\":\"1.0@DFI\",\"address2\":\"1.0@DFI\"}' "
                                      "share_address '[]'")
                       + HelpExampleRpc("addpoolliquidity",
                                      "'{\"address1\":\"1.0@DFI\",\"address2\":\"1.0@DFI\"}' "
                                      "share_address '[]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }

    RPCTypeCheck(request.params, { UniValue::VOBJ, UniValue::VSTR, UniValue::VARR }, true);

    // decode
    CLiquidityMessage msg{};
    msg.from = DecodeRecipients(pwallet->chain(), request.params[0].get_obj());
    msg.shareAddress = DecodeScript(request.params[1].get_str());

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::AddPoolLiquidity)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CTxDestination ownerDest;
    const auto& txInputs = request.params[2];

    if (isNonEmptyArray(txInputs)) {
        rawTx.vin = ParseInputs(txInputs);
    } else {
        for (const auto& kv : msg.from) {
            if (!ExtractDestination(kv.first, ownerDest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid owner destination");
            }

            for (const auto& auth : GetAuthInputs(pwallet, ownerDest)) {
                if (std::find(rawTx.vin.begin(), rawTx.vin.end(), auth) == rawTx.vin.end()) {
                    rawTx.vin.push_back(auth);
                }
            }
        }
    }

    // fund
    rawTx = fund(rawTx, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyAddPoolLiquidityTx(mnview_dummy, g_chainstate->CoinsTip(), CTransaction(rawTx), targetHeight, ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}), Params().GetConsensus());

        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, request)->GetHash().GetHex();
}

UniValue removepoolliquidity(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"removepoolliquidity",
               "\nCreates (and submits to local node and network) a remove pool liquidity transaction.\n"
               "The last optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                       {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "The defi address which has tokens"},
                       {"amount", RPCArg::Type::STR, RPCArg::Optional::NO, "Liquidity amount@Liquidity pool symbol"},
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
                       HelpExampleCli("removepoolliquidity", "from_address 1.0@LpSymbol")
                       + HelpExampleRpc("removepoolliquidity", "from_address 1.0@LpSymbol")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }

    RPCTypeCheck(request.params, { UniValue::VSTR, UniValue::VSTR, UniValue::VARR }, true);

    std::string from = request.params[0].get_str();
    std::string amount = request.params[1].get_str();

    // decode
    CRemoveLiquidityMessage msg{};
    msg.from = DecodeScript(from);
    msg.amount = DecodeAmount(pwallet->chain(), amount, from);

    // encode
    CDataStream markedMetadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    markedMetadata << static_cast<unsigned char>(CustomTxType::RemovePoolLiquidity)
                   << msg;
    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(markedMetadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CTxDestination ownerDest;
    if (!ExtractDestination(msg.from, ownerDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid owner destination");
    }

    const auto& txInputs = request.params[2];
    rawTx.vin = isNonEmptyArray(txInputs) ? ParseInputs(txInputs)
                                          : GetAuthInputs(pwallet, ownerDest);

    // fund
    rawTx = fund(rawTx, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyRemovePoolLiquidityTx(mnview_dummy, g_chainstate->CoinsTip(), CTransaction(rawTx), targetHeight, ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, msg}), Params().GetConsensus());

        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, request)->GetHash().GetHex();
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
                       HelpExampleCli("utxostoaccount", "'{\"address1\":\"1.0@DFI\","
                                                     "\"address2\":[\"2.0@BTC\", \"3.0@ETH\"]"
                                                     "}' '[]'")
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

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

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
    rawTx = fund(rawTx, pwallet);

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

    return signsend(rawTx, request)->GetHash().GetHex();
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
                                                     "'{\"address1\":\"1.0@DFI\",\"address2\":[\"2.0@BTC\", \"3.0@ETH\"]}' "
                                                     "'[]'")
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

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));
    CTxDestination ownerDest;
    if (!ExtractDestination(msg.from, ownerDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid owner destination");
    }

    const auto& txInputs = request.params[2];
    rawTx.vin = isNonEmptyArray(txInputs) ? ParseInputs(txInputs)
                                          : GetAuthInputs(pwallet, ownerDest);

    // fund
    rawTx = fund(rawTx, pwallet);

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

    return signsend(rawTx, request)->GetHash().GetHex();
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
                       HelpExampleCli("accounttoutxos", "sender_address '{\"address1\":\"100@DFI\"}' '[]'")
                       + HelpExampleCli("accounttoutxos", "sender_address '{\"address1\":\"1.0@DFI\",\"address2\":[\"2.0@BTC\", \"3.0@ETH\"]}' '[]'")
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

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    // auth
    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    CTxDestination ownerDest;
    if (!ExtractDestination(msg.from, ownerDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid owner destination");
    }

    const auto& txInputs = request.params[2];
    rawTx.vin = isNonEmptyArray(txInputs) ? ParseInputs(txInputs)
                                          : GetAuthInputs(pwallet, ownerDest);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    // fund
    rawTx = fund(rawTx, pwallet);

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

    return signsend(rawTx, request)->GetHash().GetHex();
}

UniValue createpoolpair(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"createpoolpair",
               "\nCreates (and submits to local node and network) a poolpair transaction with given metadata.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                       {
                            {"tokenA", RPCArg::Type::STR, RPCArg::Optional::NO,
                            "One of the keys may be specified (id/symbol)"},
                            {"tokenB", RPCArg::Type::STR, RPCArg::Optional::NO,
                            "One of the keys may be specified (id/symbol)"},
                            {"commission", RPCArg::Type::NUM, RPCArg::Optional::NO,
                            "Pool commission, up to 10^-8"},
                            {"status", RPCArg::Type::BOOL, RPCArg::Optional::NO,
                            "Pool Status: True is Active, False is Restricted"},
                            {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::NO,
                            "Address of the pool owner."},
                            {"pairSymbol", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                             "Pair symbol (unique), no longer than " +
                             std::to_string(CToken::MAX_TOKEN_SYMBOL_LENGTH)},
                       },
                   },
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
                       HelpExampleCli("createpoolpair",   "'{\"tokenA\":\"MyToken1\","
                                                          "\"tokenB\":\"MyToken2\","
                                                          "\"commission\":\"0.001\","
                                                          "\"status\":\"True\","
                                                          "\"ownerAddress\":\"Address\""
                                                          "}' '[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("createpoolpair", "'{\"tokenA\":\"MyToken1\","
                                                          "\"tokenB\":\"MyToken2\","
                                                          "\"commission\":\"0.001\","
                                                          "\"status\":\"True\","
                                                          "\"ownerAddress\":\"Address\""
                                                            "}' '[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);

    std::string tokenA, tokenB, pairSymbol;
    CAmount commission = 0; // !!!
    CScript ownerAddress;
    bool status = true; // default Active
    UniValue metadataObj = request.params[0].get_obj();
    if (!metadataObj["tokenA"].isNull()) {
        tokenA = metadataObj["tokenA"].getValStr();
    }
    if (!metadataObj["tokenB"].isNull()) {
        tokenB = metadataObj["tokenB"].getValStr();
    }
    if (!metadataObj["commission"].isNull()) {
        commission = AmountFromValue(metadataObj["commission"]);
    }
    if (!metadataObj["status"].isNull()) {
        status = metadataObj["status"].getBool();
    }
    if (!metadataObj["ownerAddress"].isNull()) {
        ownerAddress = DecodeScript(metadataObj["ownerAddress"].getValStr());
    }
    if (!metadataObj["pairSymbol"].isNull()) {
        pairSymbol = metadataObj["pairSymbol"].getValStr();
    }

    int targetHeight;
    DCT_ID idtokenA, idtokenB;
    {
        LOCK(cs_main);

        auto token = pcustomcsview->GetTokenGuessId(tokenA, idtokenA);
        if (!token)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "TokenA was not found");

        auto token2 = pcustomcsview->GetTokenGuessId(tokenB, idtokenB);
        if (!token2)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "TokenB was not found");

        targetHeight = ::ChainActive().Height() + 1;
    }

    CPoolPairMessage poolPairMsg;
    poolPairMsg.idTokenA = idtokenA;
    poolPairMsg.idTokenB = idtokenB;
    poolPairMsg.commission = commission;
    poolPairMsg.status = status;
    poolPairMsg.ownerAddress = ownerAddress;

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::CreatePoolPair)
             << poolPairMsg << pairSymbol;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    const auto& txInputs = request.params[1];
    rawTx.vin = isNonEmptyArray(txInputs) ? ParseInputs(txInputs)
                                          : GetMineAuthInputs(pwallet);

    if(rawTx.vin.empty())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Incorrect Authorization");

    rawTx = fund(rawTx, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyCreatePoolPairTx(mnview_dummy, g_chainstate->CoinsTip(), CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, poolPairMsg, pairSymbol}), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, request)->GetHash().GetHex();
}

UniValue updatepoolpair(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"updatepoolpair",
               "\nCreates (and submits to local node and network) a pool status update transaction.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend. One of UTXO's must belong to the pool's owner (collateral) address" +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                       {
                           {"pool", RPCArg::Type::STR, RPCArg::Optional::NO, "The pool's symbol, id or creation tx"},
                           {"status", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Pool Status new property (bool)"},
                           {"commission", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Pool commission, up to 10^-8"},
                           {"ownerAddress", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Address of the pool owner."},

                       },
                   },
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
                       HelpExampleCli("updatepoolpair", "'{\"pool\":\"POOL\",\"status\":true,"
                                                     "\"commission\":0.01,\"ownerAddress\":\"Address\"}' "
                                                     "'[{\"txid\":\"id\",\"vout\":0}]'")
                       + HelpExampleRpc("updatepoolpair", "'{\"pool\":\"POOL\",\"status\":true,"
                                                       "\"commission\":0.01,\"ownerAddress\":\"Address\"}' "
                                                       "'[{\"txid\":\"id\",\"vout\":0}]'")
               },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);

    bool status = true;
    CAmount commission = -1;
    CScript ownerAddress;
    UniValue metaObj = request.params[0].get_obj();

    std::string const poolStr = trim_ws(metaObj["pool"].getValStr());
    CTxDestination ownerDest;
    DCT_ID poolId;
    int targetHeight;
    {
        LOCK(cs_main);
        auto token = pcustomcsview->GetTokenGuessId(poolStr, poolId);
        if (!token) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Pool %s does not exist!", poolStr));
        }

        auto pool = pcustomcsview->GetPoolPair(poolId);
        if (!pool) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Pool %s does not exist!", poolStr));
        }
        status = pool->status;
        targetHeight = ::ChainActive().Height() + 1;
    }

    if (!metaObj["status"].isNull()) {
        status = metaObj["status"].getBool();
    }
    if (!metaObj["commission"].isNull()) {
        commission = AmountFromValue(metaObj["commission"]);
    }
    if (!metaObj["ownerAddress"].isNull()) {
        ownerAddress = DecodeScript(metaObj["ownerAddress"].getValStr());
    }

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    const auto& txInputs = request.params[1];
    rawTx.vin = isNonEmptyArray(txInputs) ? ParseInputs(txInputs)
                                          : GetMineAuthInputs(pwallet);
    if(rawTx.vin.empty())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Incorrect Authorization");

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::UpdatePoolPair)
             << poolId << status << commission << ownerAddress;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    rawTx = fund(rawTx, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyUpdatePoolPairTx(mnview_dummy, ::ChainstateActive().CoinsTip(), CTransaction(rawTx), targetHeight,
                                               ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, poolId, status, commission, ownerAddress}), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, request)->GetHash().GetHex();
}

void CheckAndFillPoolSwapMessage(const JSONRPCRequest& request, CPoolSwapMessage &poolSwapMsg) {
    std::string tokenFrom, tokenTo;
    UniValue metadataObj = request.params[0].get_obj();
    if (!metadataObj["from"].isNull()) {
        poolSwapMsg.from = DecodeScript(metadataObj["from"].getValStr());
    }
    if (!metadataObj["tokenFrom"].isNull()) {
        tokenFrom = metadataObj["tokenFrom"].getValStr();
    }
    if (!metadataObj["to"].isNull()) {
        poolSwapMsg.to = DecodeScript(metadataObj["to"].getValStr());
    }
    if (!metadataObj["tokenTo"].isNull()) {
        tokenTo = metadataObj["tokenTo"].getValStr();
    }
    {
        LOCK(cs_main);
        auto token = pcustomcsview->GetTokenGuessId(tokenFrom, poolSwapMsg.idTokenFrom);
        if (!token)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "TokenFrom was not found");

        auto token2 = pcustomcsview->GetTokenGuessId(tokenTo, poolSwapMsg.idTokenTo);
        if (!token2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "TokenTo was not found");

        auto decMax = std::max(token->decimal, token2->decimal);
        CAmount coin = std::pow(10, decMax);

        if (!metadataObj["amountFrom"].isNull()) {
            poolSwapMsg.amountFrom = AmountFromValue(metadataObj["amountFrom"], token->decimal);
        }

        if (!metadataObj["maxPrice"].isNull()) {
            CAmount maxPrice = AmountFromValue(metadataObj["maxPrice"], decMax);
            poolSwapMsg.maxPrice.integer = maxPrice / coin;
            poolSwapMsg.maxPrice.fraction = maxPrice % coin;
        }
        else {
            // This is only for maxPrice calculation

            auto poolPair = pcustomcsview->GetPoolPair(poolSwapMsg.idTokenFrom, poolSwapMsg.idTokenTo);
            if (!poolPair) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Can't find the poolpair " + tokenFrom + "-" + tokenTo);
            }
            CPoolPair const & pool = poolPair->second;
            CAmount minimumLiquidity = CPoolPair::MINIMUM_LIQUIDITY * COIN / coin;
            if (pool.totalLiquidity <= minimumLiquidity) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Pool is empty!");
            }

            auto resA = pool.reserveA;
            auto resB = pool.reserveB;
            CAmount decA = std::pow(10, decMax - token->decimal);
            CAmount decB = std::pow(10, decMax - token2->decimal);
            if (poolSwapMsg.idTokenFrom != pool.idTokenA) {
                std::swap(resA, resB);
                std::swap(decA, decB);
            }
            auto price256 = arith_uint256(resB) * decB * coin / resA / decA;
            price256 += price256 * 3 / 100; // +3%
            // this should not happen IRL, but for sure:
            if (price256.bits() > AMOUNT_BITS) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Current price +3% overflow!");
            }
            auto priceInt = price256 / coin;
            poolSwapMsg.maxPrice.integer = priceInt.GetLow64();
            poolSwapMsg.maxPrice.fraction = (price256 - priceInt * coin).GetLow64(); // cause there is no operator "%"
        }
    }
}

UniValue poolswap(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"poolswap",
               "\nCreates (and submits to local node and network) a poolswap transaction with given metadata.\n"
               "The second optional argument (may be empty array) is an array of specific UTXOs to spend." +
               HelpRequiringPassphrase(pwallet) + "\n",
               {
                   {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                       {
                           {"from", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "Address of the owner of tokenA."},
                           {"tokenFrom", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "One of the keys may be specified (id/symbol)"},
                           {"amountFrom", RPCArg::Type::NUM, RPCArg::Optional::NO,
                                       "tokenFrom coins amount"},
                           {"to", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "Address of the owner of tokenB."},
                           {"tokenTo", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "One of the keys may be specified (id/symbol)"},
                           {"maxPrice", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                       "Maximum acceptable price"},
                       },
                   },
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
                                 HelpExampleCli("poolswap",   "'{\"from\":\"MyAddress\","
                                                                    "\"tokenFrom\":\"MyToken1\","
                                                                    "\"amountFrom\":\"0.001\","
                                                                    "\"to\":\"Address\","
                                                                    "\"tokenTo\":\"Token2\","
                                                                    "\"maxPrice\":\"0.01\""
                                                                    "}' '[{\"txid\":\"id\",\"vout\":0}]'")
                                         + HelpExampleRpc("poolswap", "'{\"from\":\"MyAddress\","
                                                                            "\"tokenFrom\":\"MyToken1\","
                                                                            "\"amountFrom\":\"0.001\","
                                                                            "\"to\":\"Address\","
                                                                            "\"tokenTo\":\"Token2\","
                                                                            "\"maxPrice\":\"0.01\""
                                                                            "}' '[{\"txid\":\"id\",\"vout\":0}]'")
                             },
              }.Check(request);


    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);

    CPoolSwapMessage poolSwapMsg{};
    CheckAndFillPoolSwapMessage(request, poolSwapMsg);
    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::PoolSwap)
             << poolSwapMsg;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    CTxDestination ownerDest;
    if (!ExtractDestination(poolSwapMsg.from, ownerDest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid owner destination");
    }

    const auto& txInputs = request.params[1];
    rawTx.vin = isNonEmptyArray(txInputs) ? ParseInputs(txInputs)
                                          : GetAuthInputs(pwallet, ownerDest);

    // fund
    rawTx = fund(rawTx, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplyPoolSwapTx(mnview_dummy, ::ChainstateActive().CoinsTip(), CTransaction(rawTx), targetHeight,
                                      ToByteVector(CDataStream{SER_NETWORK, PROTOCOL_VERSION, poolSwapMsg}), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, request)->GetHash().GetHex();
}

UniValue testpoolswap(const JSONRPCRequest& request) {

    RPCHelpMan{"testpoolswap",
               "\nTests a poolswap transaction with given metadata and returns poolswap result.\n",
               {
                   {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                       {
                           {"from", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "Address of the owner of tokenA."},
                           {"tokenFrom", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "One of the keys may be specified (id/symbol)"},
                           {"amountFrom", RPCArg::Type::NUM, RPCArg::Optional::NO,
                                       "tokenFrom coins amount"},
                           {"to", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "Address of the owner of tokenB."},
                           {"tokenTo", RPCArg::Type::STR, RPCArg::Optional::NO,
                                       "One of the keys may be specified (id/symbol)"},
                           {"maxPrice", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                       "Maximum acceptable price"},
                       },
                   },
               },
               RPCResult{
                          "\"amount@tokenId\"    (string) The string with amount result of poolswap in format AMOUNT@TOKENID.\n"
               },
               RPCExamples{
                    HelpExampleCli("testpoolswap",   "'{\"from\":\"MyAddress\","
                                                    "\"tokenFrom\":\"MyToken1\","
                                                    "\"amountFrom\":\"0.001\","
                                                    "\"to\":\"Address\","
                                                    "\"tokenTo\":\"Token2\","
                                                    "\"maxPrice\":\"0.01\""
                                                    "}'")
                    + HelpExampleRpc("testpoolswap", "'{\"from\":\"MyAddress\","
                                                    "\"tokenFrom\":\"MyToken1\","
                                                    "\"amountFrom\":\"0.001\","
                                                    "\"to\":\"Address\","
                                                    "\"tokenTo\":\"Token2\","
                                                    "\"maxPrice\":\"0.01\""
                                                    "}'")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VOBJ}, true);

    CPoolSwapMessage poolSwapMsg{};
    CheckAndFillPoolSwapMessage(request, poolSwapMsg);

    // test execution and get amount
    Res res = Res::Ok();
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // create dummy cache for test state writing

        auto poolPair = mnview_dummy.GetPoolPair(poolSwapMsg.idTokenFrom, poolSwapMsg.idTokenTo);

        const std::string base{"PoolSwap creation: " + poolSwapMsg.ToString()};

        CPoolPair pp = poolPair->second;
        res = pp.Swap({poolSwapMsg.idTokenFrom, poolSwapMsg.amountFrom}, poolSwapMsg.maxPrice, [&] (const CTokenAmount &tokenAmount) {
            auto resPP = mnview_dummy.SetPoolPair(poolPair->first, pp);
            if (!resPP.ok) {
                return Res::Err("%s: %s", base, resPP.msg);
            }

            auto sub = mnview_dummy.SubBalance(poolSwapMsg.from, {poolSwapMsg.idTokenFrom, poolSwapMsg.amountFrom});
            if (!sub.ok) {
                return Res::Err("%s: %s", base, sub.msg);
            }
            uint8_t decimal = 8;
            if (auto token = mnview_dummy.GetToken(tokenAmount.nTokenId))
                decimal = token->decimal;
            return Res::Ok(tokenAmount.ToString(decimal));
        });

        if (!res.ok)
            throw JSONRPCError(RPC_VERIFY_ERROR, res.msg);
    }
    return UniValue(res.msg);
}

UniValue poolShareToJSON(DCT_ID const & poolId, CScript const & provider, CAmount const& amount, CPoolPair const& poolPair, bool verbose) {
    UniValue poolObj(UniValue::VOBJ);
    poolObj.pushKV("poolID", poolId.ToString());
    poolObj.pushKV("owner", ScriptToString(provider));
    poolObj.pushKV("%", uint64_t(amount*100/poolPair.totalLiquidity));

    if (verbose) {
        poolObj.pushKV("amount", ValueFromAmount(amount));
        poolObj.pushKV("totalLiquidity", ValueFromAmount(poolPair.totalLiquidity));
    }

    UniValue ret(UniValue::VOBJ);
    ret.pushKV(poolId.ToString() + "@" + ScriptToString(provider), poolObj);
    return ret;
}

UniValue listpoolshares(const JSONRPCRequest& request) {
    RPCHelpMan{"listpoolshares",
               "\nReturns information about pool shares.\n",
               {
                        {"pagination", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                            {
                                 {"start", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Optional first key to iterate from, in lexicographical order."},
                                 {"including_start", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                  "If true, then iterate including starting position. False by default"},
                                 {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Maximum number of pools to return, 100 by default"},
                            },
                        },
                        {"verbose", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED,
                                    "Flag for verbose list (default = true), otherwise only % are shown."},
               },
               RPCResult{
                       "{id:{...},...}     (array) Json object with pools information\n"
               },
               RPCExamples{
                       HelpExampleCli("listpoolshares", "'{\"start\":128}' False")
                       + HelpExampleRpc("listpoolshares", "'{\"start\":128}' False")
               },
    }.Check(request);

    bool verbose = true;
    if (request.params.size() > 1) {
        verbose = request.params[1].getBool();
    }

    // parse pagination
    size_t limit = 100;
    DCT_ID start{0};
    bool including_start = true;
    {
        if (request.params.size() > 0) {
            UniValue paginationObj = request.params[0].get_obj();
            if (!paginationObj["limit"].isNull()) {
                limit = (size_t) paginationObj["limit"].get_int64();
            }
            if (!paginationObj["start"].isNull()) {
                including_start = false;
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

    PoolShareKey startKey{ start, CScript{} };
//    startKey.poolID = start;
//    startKey.owner = CScript(0);

    UniValue ret(UniValue::VOBJ);
    pcustomcsview->ForEachPoolShare([&](DCT_ID const & poolId, CScript const & provider) {
        const CTokenAmount tokenAmount = pcustomcsview->GetBalance(provider, poolId);
        if(tokenAmount.nValue) {
            const auto poolPair = pcustomcsview->GetPoolPair(poolId);
            if(poolPair) {
                ret.pushKVs(poolShareToJSON(poolId, provider, tokenAmount.nValue, *poolPair, verbose));
            }
        }
        limit--;
        return limit != 0;
    }, startKey);

    return ret;
}

UniValue accounthistoryToJSON(CScript const & owner, uint32_t height, uint32_t txn, uint256 const & txid, unsigned char category, TAmounts const & diffs) {
    UniValue obj(UniValue::VOBJ);

    obj.pushKV("owner", ScriptToString(owner));
    obj.pushKV("blockHeight", (uint64_t) height);
    obj.pushKV("type", ToString(CustomTxCodeToType(category)));
    if (!txid.IsNull()) {
        obj.pushKV("txn", (uint64_t) txn);
        obj.pushKV("txid", txid.ToString());
    }

    UniValue diffsObj(UniValue::VARR);
    for (auto const & diff : diffs) {
        auto token = pcustomcsview->GetToken(diff.first);
        std::string const tokenIdStr = token->CreateSymbolKey(diff.first);

        diffsObj.push_back(ValueFromAmount(diff.second).getValStr() + "@" + tokenIdStr);
    }
    obj.pushKV("amounts", diffsObj);
    return obj;
}

UniValue listaccounthistory(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);
    RPCHelpMan{"listaccounthistory",
               "\nReturns information about account history.\n",
               {
                        {"owner", RPCArg::Type::STR, RPCArg::Optional::OMITTED,
                                    "Single account ID (CScript or address) or reserved words: \"mine\" - to list history for all owned accounts or \"all\" to list whole DB (default = \"mine\")."},
                        {"options", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                            {
                                 {"maxBlockHeight", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Optional height to iterate from (downto genesis block), (default = chaintip)."},
                                 {"depth", RPCArg::Type::NUM, RPCArg::Optional::OMITTED,
                                  "Maximum depth, 100 blocks by default for every account"},
                            },
                        },
               },
               RPCResult{
                       "[{},{}...]     (array) Objects with account history information\n"
               },
               RPCExamples{
                       HelpExampleCli("listaccounthistory", "all '{\"maxBlockHeight\":160,\"depth\":10}'")
                       + HelpExampleRpc("listaccounthistory", "address False")
               },
    }.Check(request);

    std::string accounts = "mine";
    if (request.params.size() > 0) {
        accounts = request.params[0].getValStr();
    }

    uint32_t startBlock = std::numeric_limits<uint32_t>::max();
    uint32_t depth = 100;

    if (request.params.size() > 1) {
        UniValue optionsObj = request.params[1].get_obj();
        RPCTypeCheckObj(optionsObj,
            {
                {"maxBlockHeight", UniValueType(UniValue::VNUM)},
                {"depth", UniValueType(UniValue::VNUM)},
            }, true, true);

        if (!optionsObj["maxBlockHeight"].isNull()) {
            startBlock = (uint32_t) optionsObj["maxBlockHeight"].get_int64();
        }
        if (!optionsObj["depth"].isNull()) {
            depth = (uint32_t) optionsObj["depth"].get_int64();
        }
    }

    pwallet->BlockUntilSyncedToCurrentChain();
    startBlock = std::min(startBlock, uint32_t(chainHeight(*pwallet->chain().lock())));

    UniValue ret(UniValue::VARR);

    LOCK(cs_main);

    if (accounts == "mine") {
        // traversing through owned scripts
        CScript prevOwner{};
        bool isMine = false;
        AccountHistoryKey startKey{ prevOwner, startBlock, std::numeric_limits<uint32_t>::max() }; // starting from max txn values
        pcustomcsview->ForEachAccountHistory([&](CScript const & owner, uint32_t height, uint32_t txn, uint256 const & txid, unsigned char category, TAmounts const & diffs) {
            if (height > startKey.blockHeight || (depth <= startKey.blockHeight && (height < startKey.blockHeight - depth)))
                return true; // continue

            if (prevOwner != owner) {
                prevOwner = owner;
                isMine = IsMine(*pwallet, owner) == ISMINE_SPENDABLE;
            }
            if (isMine)
                ret.push_back(accounthistoryToJSON(owner, height, txn, txid, category, diffs));

            return true;
        }, startKey);
    }
    else if (accounts == "all") {
        // traversing the whole DB, skipping wrong heights
        AccountHistoryKey startKey{ CScript{}, startBlock, std::numeric_limits<uint32_t>::max() }; // starting from max txn values
        pcustomcsview->ForEachAccountHistory([&](CScript const & owner, uint32_t height, uint32_t txn, uint256 const & txid, unsigned char category, TAmounts const & diffs) {
            if (height > startKey.blockHeight || (depth <= startKey.blockHeight && (height < startKey.blockHeight - depth)))
                return true; // continue

            ret.push_back(accounthistoryToJSON(owner, height, txn, txid, category, diffs));
            return true;
        }, startKey);

    }
    else {
        // parse single script/address:
        CScript const owner = DecodeScript(accounts);

        AccountHistoryKey startKey{ owner, startBlock, std::numeric_limits<uint32_t>::max() }; // starting from max txn values
        pcustomcsview->ForEachAccountHistory([&](CScript const & owner, uint32_t height, uint32_t txn, uint256 const & txid, unsigned char category, TAmounts const & diffs) {
            if (owner != startKey.owner || (height > startKey.blockHeight || (depth <= startKey.blockHeight && (height < startKey.blockHeight - depth))))
                return false;

            ret.push_back(accounthistoryToJSON(owner, height, txn, txid, category, diffs));
            return true;
        }, startKey);
    }

    return ret;
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

    UniValue ret(UniValue::VOBJ);

    LOCK(cs_main);
    for (auto kv : Params().GetConsensus().nonUtxoBlockSubsidies) {
        ret.pushKV(GetCommunityAccountName(kv.first), ValueFromAmount(pcustomcsview->GetCommunityBalance(kv.first)));
    }

    return ret;
}

UniValue setgov(const JSONRPCRequest& request) {
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"setgov",
               "\nSet special 'governance' variables. Two types of them implemented for now: LP_SPLITS and LP_DAILY_DFI_REWARD\n",
               {
                    {"variables", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Object with variables",
                        {
                            {"name", RPCArg::Type::STR, RPCArg::Optional::NO, "Variable's name is the key, value is the data. Exact data type depends on variable's name."},
                        },
                    },
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
                       HelpExampleCli("setgov", "'{\"LP_SPLITS\": {\"2\":0.2,\"3\":0.8}'")
                       + HelpExampleRpc("setgov", "'{\"LP_DAILY_DFI_REWARD\":109440}'")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VOBJ, UniValue::VARR}, true);

    CDataStream varStream(SER_NETWORK, PROTOCOL_VERSION);
    if (request.params.size() > 0 && request.params[0].isObject()) {
        for (const std::string& name : request.params[0].getKeys()) {
            auto gv = GovVariable::Create(name);
            if(!gv)
                throw JSONRPCError(RPC_INVALID_REQUEST, "Variable " + name + " not registered");
            gv->Import(request.params[0][name]);
            varStream << name << *gv;
        }
    }

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::SetGovVariable)
             << varStream;

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    int targetHeight = chainHeight(*pwallet->chain().lock()) + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    const auto& txInputs = request.params[1];
    rawTx.vin = isNonEmptyArray(txInputs) ? ParseInputs(txInputs)
                                          : GetMineAuthInputs(pwallet);
    if(rawTx.vin.empty())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Incorrect Authorization");

    rawTx = fund(rawTx, pwallet);

    // check execution
    {
        LOCK(cs_main);
        CCustomCSView mnview_dummy(*pcustomcsview); // don't write into actual DB
        const auto res = ApplySetGovernanceTx(mnview_dummy, g_chainstate->CoinsTip(), CTransaction(rawTx), targetHeight,
                                      ToByteVector(varStream), Params().GetConsensus());
        if (!res.ok) {
            throw JSONRPCError(RPC_INVALID_REQUEST, "Execution test failed:\n" + res.msg);
        }
    }
    return signsend(rawTx, request)->GetHash().GetHex();
}

UniValue getgov(const JSONRPCRequest& request) {
    RPCHelpMan{"getgov",
               "\nReturns information about governance variable. Two types of them implemented for now: LP_SPLITS and LP_DAILY_DFI_REWARD\n",
               {
                       {"name", RPCArg::Type::STR, RPCArg::Optional::NO,
                        "Variable name"},
               },
               RPCResult{
                       "{id:{...}}     (array) Json object with variable information\n"
               },
               RPCExamples{
                       HelpExampleCli("getgov", "LP_SPLITS")
                       + HelpExampleRpc("getgov", "LP_DAILY_DFI_REWARD")
               },
    }.Check(request);

    LOCK(cs_main);

    auto name = request.params[0].getValStr();
    auto var = pcustomcsview->GetVariable(name);
    if (var) {
        UniValue ret(UniValue::VOBJ);
        ret.pushKV(var->GetName(), var->Export());
        return ret;
    }
    throw JSONRPCError(RPC_INVALID_REQUEST, "Variable '" + name + "' not registered");
}

UniValue isappliedcustomtx(const JSONRPCRequest& request) {
    RPCHelpMan{"isappliedcustomtx",
               "\nChecks that custom transaction was affected on chain\n",
               {
                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::OMITTED, "A transaction hash"},
                    {"blockHeight", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "The height of block which contain tx"}
               },
               RPCResult{
                       "(bool) The boolean indicate that custom transaction was affected on chain\n"
               },
               RPCExamples{
                       HelpExampleCli("isappliedcustomtx", "b2bb09ffe9f9b292f13d23bafa1225ef26d0b9906da7af194c5738b63839b235 1005")
                       + HelpExampleRpc("isappliedcustomtx", "b2bb09ffe9f9b292f13d23bafa1225ef26d0b9906da7af194c5738b63839b235 1005")
               },
    }.Check(request);

    RPCTypeCheck(request.params, {UniValue::VSTR, UniValue::VNUM}, false);

    LOCK(cs_main);

    UniValue result(UniValue::VBOOL);

    uint256 txHash = ParseHashV(request.params[0], "txid");
    int blockHeight = request.params[1].get_int();

    const auto undo = pcustomcsview->GetUndo(UndoKey{static_cast<uint32_t>(blockHeight), txHash});

    if (!undo) { // no changes done
        result.setBool(false);
    } else {
        result.setBool(true);
    }

    return result;
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
    {"tokens",      "updatetoken",        &updatetoken,        {"token", "metadata", "inputs"}},
    {"tokens",      "listtokens",         &listtokens,         {"pagination", "verbose"}},
    {"tokens",      "gettoken",           &gettoken,           {"key" }},
    {"tokens",      "minttokens",         &minttokens,         {"amounts", "inputs"}},
    {"accounts",    "listaccounts",       &listaccounts,       {"pagination", "verbose", "indexed_amounts", "is_mine_only"}},
    {"accounts",    "getaccount",         &getaccount,         {"owner", "pagination", "indexed_amounts"}},
    {"poolpair",    "listpoolpairs",      &listpoolpairs,      {"pagination", "verbose"}},
    {"poolpair",    "getpoolpair",        &getpoolpair,        {"key", "verbose" }},
    {"poolpair",    "addpoolliquidity",   &addpoolliquidity,   {"from", "shareAddress", "inputs"}},
    {"poolpair",    "removepoolliquidity",&removepoolliquidity,{"from", "amount", "inputs"}},
    {"accounts",    "gettokenbalances",   &gettokenbalances,   {"pagination", "indexed_amounts", "symbol_lookup"}},
    {"accounts",    "utxostoaccount",     &utxostoaccount,     {"amounts", "inputs"}},
    {"accounts",    "accounttoaccount",   &accounttoaccount,   {"from", "to", "inputs"}},
    {"accounts",    "accounttoutxos",     &accounttoutxos,     {"from", "to", "inputs"}},
    {"poolpair",    "createpoolpair",     &createpoolpair,     {"metadata", "inputs"}},
    {"poolpair",    "updatepoolpair",     &updatepoolpair,     {"metadata", "inputs"}},
    {"poolpair",    "poolswap",           &poolswap,           {"metadata", "inputs"}},
    {"poolpair",    "listpoolshares",     &listpoolshares,     {"pagination", "verbose"}},
    {"poolpair",    "testpoolswap",       &testpoolswap,       {"metadata"}},
    {"accounts",    "listaccounthistory", &listaccounthistory, {"owner", "options"}},
    {"accounts",    "listcommunitybalances", &listcommunitybalances, {}},
    {"blockchain",  "setgov",             &setgov,             {"variables", "inputs"}},
    {"blockchain",  "getgov",             &getgov,             {"name"}},
    {"blockchain",  "isappliedcustomtx",  &isappliedcustomtx,  {"txid", "blockHeight"}},
};

void RegisterMasternodesRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
