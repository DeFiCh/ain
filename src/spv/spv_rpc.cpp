// Copyright (c) 2019 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <chainparams.h>
#include <core_io.h>
#include <rpc/client.h>
#include <rpc/server.h>
#include <rpc/protocol.h>
#include <rpc/util.h>
#include <masternodes/anchors.h>
#include <masternodes/masternodes.h>
#include <spv/btctransaction.h>
#include <spv/spv_wrapper.h>
#include <univalue/include/univalue.h>

//#ifdef ENABLE_WALLET
#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>
//#endif

#include <stdexcept>
#include <future>

static CWallet* GetWallet(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    EnsureWalletIsAvailable(pwallet, false);
    EnsureWalletIsUnlocked(pwallet);
    return pwallet;
}

static const int ENOSPV         = 100000;
static const int EPARSINGTX     = 100001;
static const int ETXNOTSIGNED   = 100002;

std::string DecodeSendResult(int result)
{
    switch (result) {
        case ENOSPV:
            return "spv module disabled";
        case EPARSINGTX:
            return "Can't parse transaction";
        case ETXNOTSIGNED:
            return "Tx not signed";
        default:
            return strerror(result);
    }
}

UniValue spv_sendrawtx(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_sendrawtx",
        "\nSending raw tx to DeFi Blockchain\n",
        {
            {"rawtx", RPCArg::Type::STR, RPCArg::Optional::NO, "The hex-encoded raw transaction with signature" },
        },
        RPCResult{
            "\"none\"                  Returns nothing\n"
        },
        RPCExamples{
            HelpExampleCli("spv_sendrawtx", "\"rawtx\"")
            + HelpExampleRpc("spv_sendrawtx", "\"rawtx\"")
        },
    }.Check(request);

    if (!spv::pspv)
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");

    std::promise<int> promise;
    if (spv::pspv->SendRawTx(ParseHexV(request.params[0], "rawtx"), &promise)) {
        int sendResult = promise.get_future().get();
        if (sendResult != 0)
            throw JSONRPCError(RPC_INVALID_REQUEST, DecodeSendResult(sendResult));
    }
    else {
        throw JSONRPCError(RPC_INVALID_REQUEST, "Can't parse transaction");
    }

    return UniValue("");
}

/*
 * For tests|experiments only
*/
UniValue spv_splitutxo(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_splitutxo",
        "\nFor tests|experiments only\n",
        {
            {"parts", RPCArg::Type::NUM, RPCArg::Optional::NO, "Number of parts" },
            {"amount", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Amount of each part, optional"},
        },
        RPCResult{
            "\"txHex\"                  (string) The hex-encoded raw transaction with signature(s)\n"
            "\"txHash\"                 (string) The hex-encoded transaction hash\n"
        },
        RPCExamples{
            HelpExampleCli("spv_splitutxo", "5 10000")
            + HelpExampleRpc("spv_splitutxo", "5 10000")
        },
    }.Check(request);

    RPCTypeCheck(request.params, { UniValue::VNUM, UniValue::VNUM }, true);

    int parts = request.params[0].get_int();
    int amount = request.params[1].empty() ? 0 : request.params[1].get_int();

    /// @todo temporary, tests
    auto rawtx = spv::CreateSplitTx("1251d1fc46d104564ca8311696d561bf7de5c0e336039c7ccfe103f7cdfc026e", 2, 3071995, "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP", parts, amount);

    bool send = false;
    if (send) {
        if (!spv::pspv)
            throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");

        spv::pspv->SendRawTx(rawtx);
    }

    CMutableBtcTransaction mtx;
    (void) DecodeHexBtcTx(mtx, std::string(rawtx.begin(), rawtx.end()), true);

    UniValue result(UniValue::VOBJ);
    result.pushKV("txHex", HexStr(rawtx));
    result.pushKV("txHash", CBtcTransaction(mtx).GetHash().ToString());

    return result;
}

extern CAmount GetAnchorSubsidy(int anchorHeight, int prevAnchorHeight, const Consensus::Params& consensusParams);

/*
 * Create, sign and send (optional) anchor tx using only spv api
 * Issued by: any
*/
UniValue spv_createanchor(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_createanchor",
        "\nCreates (and optional submits to bitcoin blockchain) an anchor tx with latest possible (every 15th) authorized blockhash.\n"
        "The first argument is the specific UTXOs to spend." +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
            {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "A json array of json objects",
                {
                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount of output in satoshis"},
                            {"privkey", RPCArg::Type::STR, RPCArg::Optional::NO, "WIF private key for signing this output"},
                        },
                    },
                },
            },
            {"rewardAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "User's P2PKH address (in DeFi chain) for reward"},
            {"send", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Send it to btc network (Default = true)"},
            {"feerate", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Feerate (satoshis) per 1000 bytes (Default = " + std::to_string(spv::DEFAULT_BTC_FEERATE) + ")"},
        },
        RPCResult{
            "\"txHex\"                  (string) The hex-encoded raw transaction with signature(s)\n"
            "\"txHash\"                 (string) The hex-encoded transaction hash\n"
        },
        RPCExamples{
            HelpExampleCli("spv_createanchor", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0,\\\"amount\\\":10000,\\\"privkey\\\":\\\"WIFprivkey\\\"}]\" "
                                            "\\\"rewardAddress\\\" True 2000"
                                            )
            + HelpExampleRpc("spv_createanchor", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0,\\\"amount\\\":10000,\\\"privkey\\\":\\\"WIFprivkey\\\"}]\" "
                                                 "\\\"rewardAddress\\\" True 2000"
                                                 )
        },
    }.Check(request);

    if (!spv::pspv)
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create anchor while still in Initial Block Download");
    }

    RPCTypeCheck(request.params, { UniValue::VARR, UniValue::VSTR, UniValue::VBOOL }, true);
    if (request.params[0].isNull() || request.params[1].isNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 and 2 must be non-null");
    }
    
    const UniValue inputs = request.params[0].get_array();
    if (inputs.empty())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Transaction input cannot be empty");
    }
    std::vector<spv::TxInputData> inputsData;
    for (size_t idx = 0; idx < inputs.size(); ++idx)
    {
        UniValue const & input = inputs[idx].get_obj();
        ParseHashV(input["txid"], "txid");
        inputsData.push_back({ input["txid"].getValStr(), input["vout"].get_int(), (uint64_t) input["amount"].get_int64(), input["privkey"].getValStr() });
    }

    std::string rewardAddress = request.params[1].getValStr();
    CTxDestination rewardDest = DecodeDestination(rewardAddress);
    if (rewardDest.which() != 1 && rewardDest.which() != 4)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "rewardAddress (" + rewardAddress + ") does not refer to a P2PKH or P2WPKH address");
    }
    bool const send = request.params[2].isNull() ? true : request.params[2].getBool();

    int64_t const feerate = request.params[3].isNull() ? spv::DEFAULT_BTC_FEERATE : request.params[3].get_int64();
    if (feerate <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Feerate should be > 0!");
    }

    THeight prevAnchorHeight{0};
    CAnchor anchor;
    {
        auto locked_chain = pwallet->chain().lock();

        anchor = panchorauths->CreateBestAnchor(rewardDest);
        prevAnchorHeight = panchors->GetActiveAnchor() ? panchors->GetActiveAnchor()->anchor.height : 0;
    }
    if (anchor.sigs.empty()) {
        throw JSONRPCError(RPC_VERIFY_ERROR, "Min anchor quorum was not reached!");
    }

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << anchor;

    uint256 hash;
    spv::TBytes rawtx;
    uint64_t cost;
    try {
        std::tie(hash, rawtx, cost) = spv::CreateAnchorTx(inputsData, ToByteVector(ss), (uint64_t) feerate);
    }
    catch (std::runtime_error const & e) {
        throw JSONRPCError(RPC_MISC_ERROR, e.what());
    }

    // after successful tx creation we does not throw!
    int sendResult = 0;
    if (send) {
        if (spv::pspv) {
            std::promise<int> promise;
            if (spv::pspv->SendRawTx(rawtx, &promise)) {
                sendResult = promise.get_future().get();
            }
            else {
                sendResult = EPARSINGTX;
            }
        }
        else
            sendResult = ENOSPV;
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txHex", HexStr(rawtx));
    result.pushKV("txHash", hash.ToString());
    result.pushKV("defiHash", anchor.blockHash.ToString());
    result.pushKV("defiHeight", (int) anchor.height);
    result.pushKV("estimatedReward", ValueFromAmount(GetAnchorSubsidy(anchor.height, prevAnchorHeight, Params().GetConsensus())));
    result.pushKV("cost", cost);
    if (send) {
        result.pushKV("sendResult", sendResult);
        result.pushKV("sendMessage", DecodeSendResult(sendResult));
    }

    return result;
}

UniValue spv_createanchortemplate(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_createanchortemplate",
        "\nCreates an anchor tx template with latest possible (every 15th) authorized blockhash.\n" +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
            {"rewardAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "User's P2PKH address (in DeFi chain) for reward"},
        },
        RPCResult{
            "\"txHex\"                  (string) The hex-encoded raw transaction with signature(s)\n"
        },
        RPCExamples{
            HelpExampleCli("spv_createanchortemplate", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0,\\\"amount\\\":10000,\\\"privkey\\\":\\\"WIFprivkey\\\"}]\" "
                                            "\\\"rewardAddress\\\""
                                            )
            + HelpExampleRpc("spv_createanchortemplate", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0,\\\"amount\\\":10000,\\\"privkey\\\":\\\"WIFprivkey\\\"}]\" "
                                                 "\\\"rewardAddress\\\""
                                                 )
        },
    }.Check(request);


    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create anchor while still in Initial Block Download");
    }

    std::string rewardAddress = request.params[0].getValStr();
    CTxDestination rewardDest = DecodeDestination(rewardAddress);
    if (rewardDest.which() != 1 && rewardDest.which() != 4) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "rewardAddress (" + rewardAddress + ") does not refer to a P2PKH or P2WPKH address");
    }

    THeight prevAnchorHeight{0};
    CAnchor anchor;
    {
        auto locked_chain = pwallet->chain().lock();

        anchor = panchorauths->CreateBestAnchor(rewardDest);
        prevAnchorHeight = panchors->GetActiveAnchor() ? panchors->GetActiveAnchor()->anchor.height : 0;
    }
    if (anchor.sigs.empty()) {
        throw JSONRPCError(RPC_VERIFY_ERROR, "Min anchor quorum was not reached!");
    }

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << anchor;
    auto metaScripts = spv::EncapsulateMeta(ToByteVector(ss));

    auto consensus = Params().GetConsensus();

    spv::TBytes scriptBytes{spv::CreateScriptForAddress(consensus.spv.anchors_address.c_str())};
    if (scriptBytes.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Can't create script for chainparam's 'spv.anchors_address' = '" + consensus.spv.anchors_address + "'");
    }
    CMutableBtcTransaction mtx;
    // output[0] - anchor address with creation fee
    mtx.vout.push_back(CBtcTxOut(consensus.spv.creationFee, CScript(scriptBytes.begin(), scriptBytes.end())));

    // output[1] - metadata (first part with OP_RETURN)
    mtx.vout.push_back(CBtcTxOut(0, metaScripts[0]));

    // output[2..n-1] - metadata (rest of the data in p2wsh keys)
    for (size_t i = 1; i < metaScripts.size(); ++i) {
        mtx.vout.push_back(CBtcTxOut(spv::P2WSH_DUST, metaScripts[i]));
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txHex", EncodeHexBtcTx(CBtcTransaction(mtx)));
    result.pushKV("defiHash", anchor.blockHash.ToString());
    result.pushKV("defiHeight", (int) anchor.height);
    result.pushKV("estimatedReward", ValueFromAmount(GetAnchorSubsidy(anchor.height, prevAnchorHeight, consensus)));
    result.pushKV("anchorAddress", Params().GetConsensus().spv.anchors_address);

    return result;
}

UniValue spv_estimateanchorcost(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_estimateanchorcost",
        "\nEstimates current anchor cost with default fee, one input and one change output.\n",
        {
            {"feerate", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Feerate (satoshis) per 1000 bytes (Default = " + std::to_string(spv::DEFAULT_BTC_FEERATE) + ")"},
        },
        RPCResult{
            "\"cost\"                  (numeric) Estimated anchor cost (satoshis)\n"
        },
        RPCExamples{
            HelpExampleCli("spv_estimateanchorcost", "")
            + HelpExampleRpc("spv_estimateanchorcost", "")
        },
    }.Check(request);

    int64_t const feerate = request.params[0].isNull() ? spv::DEFAULT_BTC_FEERATE : request.params[0].get_int64();
    if (feerate <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Feerate should be > 0!");
    }

    auto locked_chain = pwallet->chain().lock();

    // it is unable to create "pure" dummy anchor, cause it needs signing with real key
    CAnchor const anchor = panchorauths->CreateBestAnchor(CTxDestination(PKHash()));
    if (anchor.sigs.empty()) {
        throw JSONRPCError(RPC_VERIFY_ERROR, "No potential anchor, can't estimate!");
    }

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << anchor;
    return UniValue(spv::EstimateAnchorCost(ToByteVector(ss), (uint64_t) feerate));
}

UniValue spv_rescan(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_rescan",
        "\nRescan from block height...\n",
        {
            {"height", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Block height or ('tip' minus 'height') if negative)."},
        },
        RPCResult{
            "\"none\"                  Returns nothing\n"
        },
        RPCExamples{
            HelpExampleCli("spv_rescan", "600000")
            + HelpExampleRpc("spv_rescan", "600000")
        },
    }.Check(request);

    int height = request.params[0].isNull() ? 0 : request.params[0].get_int();

    if (!spv::pspv)
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");

    if (!spv::pspv->Rescan(height))
        throw JSONRPCError(RPC_MISC_ERROR, "SPV not connected");

    return {};
}

UniValue spv_syncstatus(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_syncstatus",
        "\nReturns spv sync status\n",
        {
        },
        RPCResult{
            "{                           (json object)\n"
            "   \"connected\"                (bool) Connection status\n"
            "   \"current\"                  (num) Last synced block\n"
            "   \"estimated\"                (num) Estimated chain height (as reported by peers)\n"
            "}\n"
        },
        RPCExamples{
            HelpExampleCli("spv_syncstatus", "")
            + HelpExampleRpc("spv_syncstatus", "")
        },
    }.Check(request);

    if (!spv::pspv)
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");

    UniValue result(UniValue::VOBJ);
    result.pushKV("connected", spv::pspv->IsConnected());
    result.pushKV("current", static_cast<int>(spv::pspv->GetLastBlockHeight()));
    result.pushKV("estimated", static_cast<int>(spv::pspv->GetEstimatedBlockHeight()));
    return result;
}

UniValue spv_gettxconfirmations(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_gettxconfirmations",
        "\nReports tx confirmations (if any)...\n",
        {
            {"txhash", RPCArg::Type::STR, RPCArg::Optional::NO, "Hash of tx to look for"},
        },
        RPCResult{
            "count                (num) Tx confirmations. Zero if not confirmed yet (mempooled?) and -1 if not found\n"
        },
        RPCExamples{
            HelpExampleCli("spv_gettxconfirmations", "\\\"txid\\\"")
            + HelpExampleRpc("spv_gettxconfirmations", "\\\"txid\\\"")
        },
    }.Check(request);

    uint256 txHash;
    ParseHashStr(request.params[0].getValStr(), txHash);

    // ! before cs_main lock
//    uint32_t const spvLastHeight = spv::pspv ? spv::pspv->GetLastBlockHeight() : 0;

    auto locked_chain = pwallet->chain().lock();
//    panchors->UpdateLastHeight(spvLastHeight);
    return UniValue(panchors->GetAnchorConfirmations(txHash));
}

UniValue spv_listanchors(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_listanchors",
        "\nList anchors (if any)\n",
        {
            {"minBtcHeight", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "min btc height, optional (default = -1)"},
            {"maxBtcHeight", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "max btc height, optional (default = -1)"},
            {"minConfs", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "min anchor confirmations, optional (default = -1)"},
            {"maxConfs", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "max anchor confirmations, optional (default = -1)"},
        },
        RPCResult{
            "\"array\"                  Returns array of anchors\n"
        },
        RPCExamples{
            HelpExampleCli("spv_listanchors", "1500000 -1 6 -1") // list completely confirmed anchors not older than 1500000 height
            + HelpExampleRpc("spv_listanchors", "-1 -1 0 0")    // list anchors in mempool (or -1 -1 -1 0)
        },
    }.Check(request);

    if (!spv::pspv)
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");

    RPCTypeCheck(request.params, { UniValue::VNUM, UniValue::VNUM, UniValue::VNUM, UniValue::VNUM }, true);


    int minBtcHeight = request.params.size() > 0 && !request.params[0].isNull() ? request.params[0].get_int() : -1;
    int maxBtcHeight = request.params.size() > 1 && !request.params[1].isNull() ? request.params[1].get_int() : -1;
    int minConfs  = request.params.size() > 2 && !request.params[2].isNull() ? request.params[2].get_int() : -1;
    int maxConfs  = request.params.size() > 3 && !request.params[3].isNull() ? request.params[3].get_int() : -1;

    // ! before cs_main lock
    uint32_t const tmp = spv::pspv->GetLastBlockHeight();

    auto locked_chain = pwallet->chain().lock();

    panchors->UpdateLastHeight(tmp); // may be unnecessary but for sure
    auto const * cur = panchors->GetActiveAnchor();
    UniValue result(UniValue::VARR);
    panchors->ForEachAnchorByBtcHeight([&result, &cur, minBtcHeight, maxBtcHeight, minConfs, maxConfs](const CAnchorIndex::AnchorRec & rec) {
        // from tip to genesis:
        auto confs = panchors->GetAnchorConfirmations(&rec);
        if ( (maxBtcHeight >= 0 && (int)rec.btcHeight > maxBtcHeight) || (minConfs >= 0 && confs < minConfs) )
            return true; // continue
        if ( (minBtcHeight >= 0 && (int)rec.btcHeight < minBtcHeight) || (maxConfs >= 0 && confs > maxConfs) )
            return false; // break


        CTxDestination rewardDest = rec.anchor.rewardKeyType == 1 ? CTxDestination(PKHash(rec.anchor.rewardKeyID)) : CTxDestination(WitnessV0KeyHash(rec.anchor.rewardKeyID));
        UniValue anchor(UniValue::VOBJ);
        anchor.pushKV("btcBlockHeight", static_cast<int>(rec.btcHeight));
        anchor.pushKV("btcTxHash", rec.txHash.ToString());
        anchor.pushKV("previousAnchor", rec.anchor.previousAnchor.ToString());
        anchor.pushKV("defiBlockHeight", static_cast<int>(rec.anchor.height));
        anchor.pushKV("defiBlockHash", rec.anchor.blockHash.ToString());
        anchor.pushKV("rewardAddress", EncodeDestination(rewardDest));
        anchor.pushKV("confirmations", panchors->GetAnchorConfirmations(&rec));

        // If post-fork show creation height
        uint64_t anchorCreationHeight{0};
        std::shared_ptr<std::vector<unsigned char>> prefix;
        if (rec.anchor.nextTeam.size() == 1 && GetAnchorEmbeddedData(*rec.anchor.nextTeam.begin(), anchorCreationHeight, prefix)) {
            anchor.pushKV("anchorCreationHeight", static_cast<int>(anchorCreationHeight));
        }

        anchor.pushKV("signatures", static_cast<int>(rec.anchor.sigs.size()));
        bool const isActive = cur && cur->txHash == rec.txHash;
        anchor.pushKV("active", isActive);
        if (isActive) {
            cur = panchors->GetAnchorByBtcTx(cur->anchor.previousAnchor);
        }

        result.push_back(anchor);
        return true;
    });
    return result;
}


UniValue spv_listanchorspending(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_listanchorspending",
        "\nList pending anchors (if any). Pending anchors are waiting on\n"
        "chain context to be fully validated, for example, anchors read\n"
        "from SPV while the blockchain is still syncing.",
        {},
        RPCResult{
            "\"array\"                  Returns array of pending anchors\n"
        },
        RPCExamples{
            HelpExampleCli("spv_listanchors", "") // list completely confirmed anchors not older than 1500000 height
            + HelpExampleRpc("spv_listanchors", "")    // list anchors in mempool (or -1 -1 -1 0)
        },
    }.Check(request);

    if (!spv::pspv)
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");

    auto locked_chain = pwallet->chain().lock();

    UniValue result(UniValue::VARR);
    panchors->ForEachPending([&result](uint256 const &, CAnchorIndex::AnchorRec & rec) {

        CTxDestination rewardDest = rec.anchor.rewardKeyType == 1 ? CTxDestination(PKHash(rec.anchor.rewardKeyID)) : CTxDestination(WitnessV0KeyHash(rec.anchor.rewardKeyID));
        UniValue anchor(UniValue::VOBJ);
        anchor.pushKV("btcBlockHeight", static_cast<int>(rec.btcHeight));
        anchor.pushKV("btcTxHash", rec.txHash.ToString());
        anchor.pushKV("defiBlockHeight", static_cast<int>(rec.anchor.height));
        anchor.pushKV("defiBlockHash", rec.anchor.blockHash.ToString());
        anchor.pushKV("rewardAddress", EncodeDestination(rewardDest));
        anchor.pushKV("confirmations", panchors->GetAnchorConfirmations(&rec));
        anchor.pushKV("signatures", static_cast<int>(rec.anchor.sigs.size()));

        // If post-fork show creation height
        uint64_t anchorCreationHeight{0};
        std::shared_ptr<std::vector<unsigned char>> prefix;
        if (rec.anchor.nextTeam.size() == 1 && GetAnchorEmbeddedData(*rec.anchor.nextTeam.begin(), anchorCreationHeight, prefix)) {
            anchor.pushKV("anchorCreationHeight", static_cast<int>(anchorCreationHeight));
        }

        result.push_back(anchor);
        return true;
    });

    return result;
}

UniValue spv_listanchorauths(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_listanchorauths",
        "\nList anchor auths (if any)\n",
        {
        },
        RPCResult{
            "\"array\"                  Returns array of anchor auths\n"
        },
        RPCExamples{
            HelpExampleCli("spv_listanchorauths", "")
            + HelpExampleRpc("spv_listanchorauths", "")
        },
    }.Check(request);

    auto locked_chain = pwallet->chain().lock();

    UniValue result(UniValue::VARR);
    CAnchorAuthIndex::Auth const * prev = nullptr;
    std::vector<CKeyID> signers;
    std::vector<std::string> signatories;
    const CKeyID* teamData{nullptr};
    uint64_t anchorCreationHeight{0};

    panchorauths->ForEachAnchorAuthByHeight([&](const CAnchorAuthIndex::Auth & auth) {
        if (!prev)
            prev = &auth;

        if (prev->GetSignHash() != auth.GetSignHash()) {
            // flush group
            UniValue item(UniValue::VOBJ);
            item.pushKV("blockHeight", static_cast<int>(prev->height));
            item.pushKV("blockHash", prev->blockHash.ToString());
            if (anchorCreationHeight != 0) {
                item.pushKV("creationHeight", static_cast<int>(anchorCreationHeight));
            }
            item.pushKV("signers", (uint64_t)signers.size());

            UniValue signees(UniValue::VARR);
            for (const auto& sigs : signatories) {
                signees.push_back(sigs);
            }

            if (!signees.empty()) {
                item.pushKV("signees", signees);
            }

            result.push_back(item);

            // clear
            signers.clear();
            signatories.clear();
            teamData = nullptr;
            anchorCreationHeight = 0;
            prev = &auth;
        }

        auto hash160 = auth.GetSigner();
        signers.push_back(hash160);

        const auto id = pcustomcsview->GetMasternodeIdByOperator(auth.GetSigner());
        if (id) {
            const auto mn = pcustomcsview->GetMasternode(*id);
            if (mn) {
                auto dest = mn->operatorType == 1 ? CTxDestination(PKHash(hash160)) : CTxDestination(WitnessV0KeyHash(hash160));
                signatories.push_back(EncodeDestination(dest));
            }
        }

        if (!teamData && prev->nextTeam.size() == 1) {
            // Team entry
            teamData = &(*prev->nextTeam.begin());

            std::shared_ptr<std::vector<unsigned char>> prefix;
            GetAnchorEmbeddedData(*teamData, anchorCreationHeight, prefix);
        }

        return true;
    });

    if (prev) {
        // place last auth group
        UniValue item(UniValue::VOBJ);
        item.pushKV("blockHeight", static_cast<int>(prev->height));
        item.pushKV("blockHash", prev->blockHash.ToString());
        if (anchorCreationHeight != 0) {
            item.pushKV("creationHeight", static_cast<int>(anchorCreationHeight));
        }
        item.pushKV("signers", (uint64_t)signers.size());

        UniValue signees(UniValue::VARR);
        for (const auto& sigs : signatories) {
            signees.push_back(sigs);
        }

        if (!signees.empty()) {
            item.pushKV("signees", signees);
        }

        result.push_back(item);
    }
    return result;
}

UniValue spv_listanchorrewardconfirms(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_listanchorrewardconfirms",
               "\nList anchor reward confirms (if any)\n",
               {
               },
               RPCResult{
                       "\"array\"                  Returns array of anchor confirms\n"
               },
               RPCExamples{
                       HelpExampleCli("spv_listanchorrewardconfirms", "")
                       + HelpExampleRpc("spv_listanchorrewardconfirms", "")
               },
    }.Check(request);

    auto locked_chain = pwallet->chain().lock();

    UniValue result(UniValue::VARR);

    CAnchorConfirmMessage const * prev = nullptr;
    std::vector<CKeyID> signers;
    panchorAwaitingConfirms->ForEachConfirm([&result, &prev, &signers](const CAnchorConfirmMessage & confirm) {
        if (!prev)
            prev = &confirm;

        if (prev->GetSignHash() != confirm.GetSignHash()) {
            // flush group
            CTxDestination rewardDest = prev->rewardKeyType == 1 ? CTxDestination(PKHash(prev->rewardKeyID)) : CTxDestination(WitnessV0KeyHash(prev->rewardKeyID));
            UniValue item(UniValue::VOBJ);
            item.pushKV("btcTxHeight", static_cast<int>(prev->btcTxHeight));
            item.pushKV("btcTxHash", prev->btcTxHash.ToString());
            item.pushKV("anchorHeight", static_cast<int>(prev->anchorHeight));
            item.pushKV("dfiBlockHash", prev->dfiBlockHash.ToString());
            item.pushKV("prevAnchorHeight", static_cast<int>(prev->prevAnchorHeight));
            item.pushKV("rewardAddress", EncodeDestination(rewardDest));
            item.pushKV("confirmSignHash", prev->GetSignHash().ToString());
            item.pushKV("signers", (uint64_t)signers.size());
            result.push_back(item);

            // clear
            signers.clear();
            prev = &confirm;
        }
        signers.push_back(confirm.GetSigner());
        return true;
    });

    if (prev) {
        // place last confirm's group
        CTxDestination rewardDest = prev->rewardKeyType == 1 ? CTxDestination(PKHash(prev->rewardKeyID)) : CTxDestination(WitnessV0KeyHash(prev->rewardKeyID));
        UniValue item(UniValue::VOBJ);
        item.pushKV("btcTxHeight", static_cast<int>(prev->btcTxHeight));
        item.pushKV("btcTxHash", prev->btcTxHash.ToString());
        item.pushKV("anchorHeight", static_cast<int>(prev->anchorHeight));
        item.pushKV("dfiBlockHash", prev->dfiBlockHash.ToString());
        item.pushKV("prevAnchorHeight", static_cast<int>(prev->prevAnchorHeight));
        item.pushKV("rewardAddress", EncodeDestination(rewardDest));
        item.pushKV("confirmSignHash", prev->GetSignHash().ToString());
        item.pushKV("signers", (uint64_t)signers.size());
        result.push_back(item);
    }
    return result;
}

UniValue spv_listanchorrewards(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_listanchorrewards",
               "\nList anchor rewards (if any)\n",
               {
               },
               RPCResult{
                       "\"array\"                  Returns array of anchor rewards\n"
               },
               RPCExamples{
                       HelpExampleCli("spv_listanchorrewards", "")
                       + HelpExampleRpc("spv_listanchorrewards", "")
               },
    }.Check(request);

    auto locked_chain = pwallet->chain().lock();

    UniValue result(UniValue::VARR);

    pcustomcsview->ForEachAnchorReward([&result] (uint256 const & btcHash, uint256 rewardHash) {
        UniValue item(UniValue::VOBJ);
        item.pushKV("AnchorTxHash", btcHash.ToString());
        item.pushKV("RewardTxHash", rewardHash.ToString());
        result.push_back(item);
        return true;
    });

    return result;
}

UniValue spv_listanchorsunrewarded(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_listanchorsunrewarded",
               "\nList anchors that have yet to be paid\n",
               {
               },
               RPCResult{
                       "\"array\"                  Returns array of anchor rewards\n"
               },
               RPCExamples{
                       HelpExampleCli("spv_listanchorrewards", "")
                       + HelpExampleRpc("spv_listanchorrewards", "")
               },
    }.Check(request);

    auto locked_chain = pwallet->chain().lock();

    UniValue result(UniValue::VARR);

    CAnchorIndex::UnrewardedResult unrewarded = panchors->GetUnrewarded();
    for (auto const & btcTxHash : unrewarded) {
        auto rec = panchors->GetAnchorByTx(btcTxHash);
        UniValue item(UniValue::VOBJ);
        item.pushKV("dfiHeight", static_cast<int>(rec->anchor.height));
        item.pushKV("dfiHash", rec->anchor.blockHash.ToString());
        item.pushKV("btcHeight", static_cast<int>(rec->btcHeight));
        item.pushKV("btcHash", btcTxHash.ToString());
        result.push_back(item);
    }

    return result;
}

UniValue spv_setlastheight(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_setlastheight",
        "\nSet last processed block height (for test purposes only)...\n",
        {
            {"height", RPCArg::Type::NUM, RPCArg::Optional::NO, "Height in btc chain"},
        },
        RPCResult{
            "\"none\"                  Returns nothing\n"
        },
        RPCExamples{
            HelpExampleCli("spv_setlastheight", "\\\"height\\\"")
            + HelpExampleRpc("spv_setlastheight", "\\\"height\\\"")
        },
    }.Check(request);

    auto fake_spv = static_cast<spv::CFakeSpvWrapper *>(spv::pspv.get());

    if (!fake_spv)
        throw JSONRPCError(RPC_INVALID_REQUEST, "command disabled");

    fake_spv->lastBlockHeight = request.params[0].get_int();
    panchors->CheckActiveAnchor(true);
    return UniValue();
}


static const CRPCCommand commands[] =
{ //  category          name                        actor (function)            params
  //  ----------------- ------------------------    -----------------------     ----------
  { "spv",      "spv_sendrawtx",              &spv_sendrawtx,             { "rawtx" }  },
  { "spv",      "spv_createanchor",           &spv_createanchor,          { "inputs", "rewardAddress", "send", "feerate" }  },
  { "spv",      "spv_createanchortemplate",   &spv_createanchortemplate,  { "rewardAddress" }  },
  { "spv",      "spv_estimateanchorcost",     &spv_estimateanchorcost,    { "feerate" }  },
  { "spv",      "spv_rescan",                 &spv_rescan,                { "height" }  },
  { "spv",      "spv_syncstatus",             &spv_syncstatus,            { }  },
  { "spv",      "spv_gettxconfirmations",     &spv_gettxconfirmations,    { "txhash" }  },
  { "spv",      "spv_splitutxo",              &spv_splitutxo,             { "parts", "amount" }  },
  { "spv",      "spv_listanchors",            &spv_listanchors,           { "minBtcHeight", "maxBtcHeight", "minConfs", "maxConfs" }  },
  { "spv",      "spv_listanchorauths",        &spv_listanchorauths,       { }  },
  { "spv",      "spv_listanchorrewardconfirms",     &spv_listanchorrewardconfirms,    { }  },
  { "spv",      "spv_listanchorrewards",      &spv_listanchorrewards,     { }  },
  { "spv",      "spv_listanchorsunrewarded",  &spv_listanchorsunrewarded, { }  },
  { "spv",      "spv_listanchorspending",     &spv_listanchorspending, { }  },
  { "hidden",   "spv_setlastheight",          &spv_setlastheight,         { "height" }  },
};

void RegisterSpvRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
