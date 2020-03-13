// Copyright (c) 2019 The DeFi Foundation
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
        "\nSending raw tx to bitcoin blockchain\n",
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


    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create anchor while still in Initial Block Download");
    }

    RPCTypeCheck(request.params, { UniValue::VARR, UniValue::VSTR, UniValue::VBOOL }, true);
    if (request.params[0].isNull() || request.params[1].isNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 and 2 must be non-null");
    }

    std::vector<spv::TxInputData> inputsData;
    UniValue inputs(UniValue::VARR);
    inputs = request.params[0].get_array();
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
        },
        RPCResult{
            "\"array\"                  Returns array of anchors\n"
        },
        RPCExamples{
            HelpExampleCli("spv_listanchors", "")
            + HelpExampleRpc("spv_listanchors", "")
        },
    }.Check(request);

    if (!spv::pspv)
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");

    // ! before cs_main lock
//    uint32_t const spvLastHeight = spv::pspv->GetLastBlockHeight();

    auto locked_chain = pwallet->chain().lock();

    auto const * top = panchors->GetActiveAnchor();
    auto const * cur = top;
    UniValue result(UniValue::VARR);
    panchors->ForEachAnchorByBtcHeight([&result, &top, &cur](const CAnchorIndex::AnchorRec & rec) {
        UniValue anchor(UniValue::VOBJ);
        anchor.pushKV("btcBlockHeight", static_cast<int>(rec.btcHeight));
        anchor.pushKV("btcTxHash", rec.txHash.ToString());
        anchor.pushKV("defiBlockHeight", static_cast<int>(rec.anchor.height));
        anchor.pushKV("defiBlockHash", rec.anchor.blockHash.ToString());
        anchor.pushKV("confirmations", panchors->GetAnchorConfirmations(&rec));
        bool const isActive = cur && cur->txHash == rec.txHash;
        anchor.pushKV("active", isActive);
        if (isActive) {
            cur = panchors->GetAnchorByBtcTx(cur->anchor.previousAnchor);
        }
        result.push_back(anchor);
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
    panchorauths->ForEachAnchorAuthByHeight([&result, &prev, &signers](const CAnchorAuthIndex::Auth & auth) {
        if (!prev)
            prev = &auth;

        if (prev->GetSignHash() != auth.GetSignHash()) {
            // flush group
            UniValue item(UniValue::VOBJ);
            item.pushKV("blockHeight", static_cast<int>(prev->height));
            item.pushKV("blockHash", prev->blockHash.ToString());
            item.pushKV("signers", signers.size());
            result.push_back(item);

            // clear
            signers.clear();
            prev = &auth;
        }
        signers.push_back(auth.GetSigner());
        return true;
    });

    if (prev) {
        // place last auth group
        UniValue item(UniValue::VOBJ);
        item.pushKV("blockHeight", static_cast<int>(prev->height));
        item.pushKV("blockHash", prev->blockHash.ToString());
        item.pushKV("signers", signers.size());
        result.push_back(item);
    }
    return result;
}

UniValue spv_listanchorconfirms(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_listanchorconfirms",
               "\nList anchor confirms (if any)\n",
               {
               },
               RPCResult{
                       "\"array\"                  Returns array of anchor confirms\n"
               },
               RPCExamples{
                       HelpExampleCli("spv_listanchorconfirms", "")
                       + HelpExampleRpc("spv_listanchorconfirms", "")
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
            UniValue item(UniValue::VOBJ);
            item.pushKV("confirmHash", prev->GetHash().ToString());
            item.pushKV("btcTxHash", prev->btcTxHash.ToString());
            item.pushKV("anchorHeight", static_cast<int>(prev->anchorHeight));
            item.pushKV("prevAnchorHeight", static_cast<int>(prev->prevAnchorHeight));
            item.pushKV("signers", signers.size());
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
        UniValue item(UniValue::VOBJ);
        item.pushKV("confirmHash", prev->GetHash().ToString());
        item.pushKV("btcTxHash", prev->btcTxHash.ToString());
        item.pushKV("anchorHeight", static_cast<int>(prev->anchorHeight));
        item.pushKV("prevAnchorHeight", static_cast<int>(prev->prevAnchorHeight));
        item.pushKV("signers", signers.size());
        result.push_back(item);
    }
    return result;
}

UniValue spv_listanchorrewards(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_listanchorrewards",
               "\nList anchor confirms (if any)\n",
               {
               },
               RPCResult{
                       "\"array\"                  Returns array of anchor confirms\n"
               },
               RPCExamples{
                       HelpExampleCli("spv_listanchorrewards", "")
                       + HelpExampleRpc("spv_listanchorrewards", "")
               },
    }.Check(request);

    auto locked_chain = pwallet->chain().lock();

    UniValue result(UniValue::VARR);

    auto rewards = pmasternodesview->ListAnchorRewards();
    for (auto && reward : rewards) { // std::map<AnchorTxHash, RewardTxHash>
        UniValue item(UniValue::VOBJ);
        item.pushKV("AnchorTxHash", reward.first.ToString());
        item.pushKV("RewardTxHash", reward.second.ToString());
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
  { "spv",      "spv_listanchors",            &spv_listanchors,           { }  },
  { "spv",      "spv_listanchorauths",        &spv_listanchorauths,       { }  },
  { "spv",      "spv_listanchorconfirms",     &spv_listanchorconfirms,    { }  },
  { "spv",      "spv_listanchorrewards",      &spv_listanchorrewards,     { }  },
  { "hidden",   "spv_setlastheight",          &spv_setlastheight,         { "height" }  },
};

void RegisterSpvRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
