// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <chainparams.h>
#include <core_io.h>
#include <rpc/server.h>
#include <rpc/protocol.h>
#include <rpc/util.h>
#include <masternodes/anchors.h>
#include <masternodes/mn_rpc.h>
#include <spv/btctransaction.h>
#include <spv/spv_wrapper.h>
#include <univalue/include/univalue.h>

//#ifdef ENABLE_WALLET
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>
//#endif

#include <stdexcept>
#include <future>

// Minimum allowed block count in HTLC contract
const uint32_t HTLC_MINIMUM_BLOCK_COUNT{9};

UniValue spv_sendrawtx(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_sendrawtx",
        "\nSending raw tx to Bitcoin blockchain\n",
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
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id of the bitcoin UTXO to spend"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output index to spend in UTXO"},
                            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount of output in satoshis"},
                            {"privkey", RPCArg::Type::STR, RPCArg::Optional::NO, "WIF private key of bitcoin for signing this output"},
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
    if (rewardDest.index() != 1 && rewardDest.index() != 4)
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
        LOCK(locked_chain->mutex());

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
        result.pushKV("sendMessage", sendResult != 0 ? DecodeSendResult(sendResult) : "");
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
            HelpExampleCli("spv_createanchortemplate",  "\\\"rewardAddress\\\"")
        },
    }.Check(request);

    if (!spv::pspv) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create anchor while still in Initial Block Download");
    }

    std::string rewardAddress = request.params[0].getValStr();
    CTxDestination rewardDest = DecodeDestination(rewardAddress);
    if (rewardDest.index() != 1 && rewardDest.index() != 4) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "rewardAddress (" + rewardAddress + ") does not refer to a P2PKH or P2WPKH address");
    }

    THeight prevAnchorHeight{0};
    CAnchor anchor;
    {
        auto locked_chain = pwallet->chain().lock();
        LOCK(locked_chain->mutex());

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
    mtx.vout.push_back(CBtcTxOut(spv::P2PKH_DUST, CScript(scriptBytes.begin(), scriptBytes.end())));

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
    LOCK(locked_chain->mutex());

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
    LOCK(locked_chain->mutex());
//    panchors->UpdateLastHeight(spvLastHeight);
    return UniValue(panchors->GetAnchorConfirmations(txHash));
}

// Populate anchors in listanchors, listanchorspending and listanchorsunrewarded
void AnchorToUniv(const CAnchorIndex::AnchorRec& rec, UniValue& anchor)
{
    CTxDestination rewardDest = rec.anchor.rewardKeyType == 1 ? CTxDestination(PKHash(rec.anchor.rewardKeyID)) : CTxDestination(WitnessV0KeyHash(rec.anchor.rewardKeyID));
    anchor.pushKV("btcBlockHeight", static_cast<int>(rec.btcHeight));
    anchor.pushKV("btcBlockHash", panchors->ReadBlockHash(rec.btcHeight).ToString());
    anchor.pushKV("btcTxHash", rec.txHash.ToString());
    anchor.pushKV("previousAnchor", rec.anchor.previousAnchor.ToString());
    anchor.pushKV("defiBlockHeight", static_cast<int>(rec.anchor.height));
    anchor.pushKV("defiBlockHash", rec.anchor.blockHash.ToString());
    anchor.pushKV("rewardAddress", EncodeDestination(rewardDest));
    anchor.pushKV("confirmations", panchors->GetAnchorConfirmations(&rec));
    anchor.pushKV("signatures", static_cast<int>(rec.anchor.sigs.size()));

    // If post-fork show creation height
    uint64_t anchorCreationHeight{0};
    std::shared_ptr<std::vector<unsigned char>> prefix;
    if (rec.anchor.nextTeam.size() == 1 && GetAnchorEmbeddedData(*rec.anchor.nextTeam.begin(), anchorCreationHeight, prefix))
    {
        anchor.pushKV("anchorCreationHeight", static_cast<int>(anchorCreationHeight));
    }
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
            {"startBtcHeight", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "max anchor confirmations, optional (default = -1)"},
            {"limit", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "number of records to return (default = unlimited)"},
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

    RPCTypeCheck(request.params, { UniValue::VNUM, UniValue::VNUM, UniValue::VNUM, UniValue::VNUM, UniValue::VNUM, UniValue::VNUM }, true);

    const int minBtcHeight = request.params.size() > 0 ? request.params[0].get_int() : -1;
    const int maxBtcHeight = request.params.size() > 1 ? request.params[1].get_int() : -1;
    const int minConfs = request.params.size() > 2 ? request.params[2].get_int() : -1;
    const int maxConfs = request.params.size() > 3 ? request.params[3].get_int() : -1;
    const int startBtcHeight = request.params.size() > 4 ? request.params[4].get_int() : -1;
    const int limit = request.params.size() > 5 ? request.params[5].get_int() : std::numeric_limits<int>::max();

    // ! before cs_main lock
    uint32_t const tmp = spv::pspv->GetLastBlockHeight();

    auto locked_chain = pwallet->chain().lock();
    LOCK(locked_chain->mutex());

    panchors->UpdateLastHeight(tmp); // may be unnecessary but for sure
    auto const * cur = panchors->GetActiveAnchor();
    auto count = limit;
    UniValue result(UniValue::VARR);
    panchors->ForEachAnchorByBtcHeight([&](const CAnchorIndex::AnchorRec & rec) {
        // from tip to genesis:
        auto confs = panchors->GetAnchorConfirmations(&rec);
        if ((maxBtcHeight >= 0 && (int)rec.btcHeight > maxBtcHeight) || (minConfs >= 0 && confs < minConfs))
            return true; // continue
        if ((minBtcHeight >= 0 && (int)rec.btcHeight < minBtcHeight) ||
            (maxConfs >= 0 && confs > maxConfs) ||
            (startBtcHeight >= 0 && static_cast<THeight>(rec.btcHeight) < startBtcHeight))
            return false; // break

        UniValue anchor(UniValue::VOBJ);
        AnchorToUniv(rec, anchor);

        bool const isActive = cur && cur->txHash == rec.txHash;
        anchor.pushKV("active", isActive);
        if (isActive) {
            cur = panchors->GetAnchorByBtcTx(cur->anchor.previousAnchor);
        }

        result.push_back(anchor);
        return --count != 0;
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
            HelpExampleCli("spv_listanchorspending", "") // list completely confirmed anchors not older than 1500000 height
            + HelpExampleRpc("spv_listanchorspending", "")    // list anchors in mempool (or -1 -1 -1 0)
        },
    }.Check(request);

    if (!spv::pspv)
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");

    auto locked_chain = pwallet->chain().lock();
    LOCK(locked_chain->mutex());

    UniValue result(UniValue::VARR);
    panchors->ForEachPending([&result](uint256 const &, CAnchorIndex::AnchorRec & rec)
    {
        UniValue anchor(UniValue::VOBJ);
        AnchorToUniv(rec, anchor);

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
    LOCK(locked_chain->mutex());

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
            item.pushKV("previousAnchor", prev->previousAnchor.ToString());
            item.pushKV("blockHeight", static_cast<int>(prev->height));
            item.pushKV("blockHash", prev->blockHash.ToString());
            item.pushKV("creationHeight", static_cast<int>(anchorCreationHeight));
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
        item.pushKV("previousAnchor", prev->previousAnchor.ToString());
        item.pushKV("blockHeight", static_cast<int>(prev->height));
        item.pushKV("blockHash", prev->blockHash.ToString());
        item.pushKV("creationHeight", static_cast<int>(anchorCreationHeight));
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
    LOCK(locked_chain->mutex());

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
    LOCK(locked_chain->mutex());

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
                       "\"array\"                  Returns array of unrewarded anchors\n"
               },
               RPCExamples{
                       HelpExampleCli("spv_listanchorsunrewarded", "")
                       + HelpExampleRpc("spv_listanchorsunrewarded", "")
               },
    }.Check(request);

    auto locked_chain = pwallet->chain().lock();
    LOCK(locked_chain->mutex());

    UniValue result(UniValue::VARR);

    CAnchorIndex::UnrewardedResult unrewarded = panchors->GetUnrewarded();
    for (auto const & btcTxHash : unrewarded) {
        auto rec = panchors->GetAnchorByTx(btcTxHash);
        UniValue item(UniValue::VOBJ);
        AnchorToUniv(*rec, item);
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

UniValue spv_decodehtlcscript(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_decodehtlcscript",
        "\nDecode and return value in a HTLC redeemscript\n",
        {
            {"redeemscript", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The HTLC redeemscript"},
        },
        RPCResult{
            "{\n"
            "  \"receiverPubkey\"            (string) The public key of the possessor of the seed\n"
            "  \"ownerPubkey\"               (string) The public key of the recipient of the refund\n"
            "  \"blocks\"                    (number) Locktime in number of blocks\n"
            "  \"hash\"                      (string) Hex-encoded seed hash if no seed provided\n"
            "}\n"
        },
        RPCExamples{
            HelpExampleCli("spv_decodehtlcscript", "\\\"redeemscript\\\"")
            + HelpExampleRpc("spv_decodehtlcscript", "\\\"redeemscript\\\"")
        },
    }.Check(request);

    if (!IsHex(request.params[0].get_str()))
    {
        throw JSONRPCError(RPC_TYPE_ERROR, "Redeemscript expected in hex format");
    }

    auto redeemBytes = ParseHex(request.params[0].get_str());
    CScript redeemScript(redeemBytes.begin(), redeemBytes.end());
    auto details = spv::GetHTLCDetails(redeemScript);

    UniValue result(UniValue::VOBJ);
    result.pushKV("sellerkey", HexStr(details.sellerKey));
    result.pushKV("buyerkey", HexStr(details.buyerKey));
    result.pushKV("blocks", static_cast<uint64_t>(details.locktime));
    result.pushKV("hash", HexStr(details.hash));

    return result;
}

CPubKey PublickeyFromString(const std::string &pubkey)
{
    if (!IsHex(pubkey) || (pubkey.length() != 66 && pubkey.length() != 130))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid public key: " + pubkey);
    }

    return HexToPubKey(pubkey);
}

CScript CreateScriptForHTLC(const JSONRPCRequest& request, uint32_t& blocks, std::vector<unsigned char>& image)
{
    CPubKey seller_key = PublickeyFromString(request.params[0].get_str());
    CPubKey refund_key = PublickeyFromString(request.params[1].get_str());

    {
        UniValue timeout;
        if (!timeout.read(std::string("[") + request.params[2].get_str() + std::string("]")) || !timeout.isArray() || timeout.size() != 1)
        {
            throw JSONRPCError(RPC_TYPE_ERROR, "Error parsing JSON: " + request.params[3].get_str());
        }

        blocks = timeout[0].get_int();
    }

    if (blocks >= CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG)
    {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid block denominated relative timeout");
    }
    else if (blocks < HTLC_MINIMUM_BLOCK_COUNT)
    {
        throw JSONRPCError(RPC_TYPE_ERROR, "Timeout below minimum of " + std::to_string(HTLC_MINIMUM_BLOCK_COUNT));
    }

    return GetScriptForHTLC(seller_key, refund_key, image, blocks);
}

UniValue spv_createhtlc(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_createhtlc",
        "\nCreates a Bitcoin address whose funds can be unlocked with a seed or as a refund.\n"
        "It returns a json object with the address and redeemScript.\n",
        {
            {"receiverPubkey", RPCArg::Type::STR, RPCArg::Optional::NO, "The public key of the possessor of the seed"},
            {"ownerPubkey", RPCArg::Type::STR, RPCArg::Optional::NO, "The public key of the recipient of the refund"},
            {"timeout", RPCArg::Type::STR, RPCArg::Optional::NO, "Timeout of the contract (denominated in blocks) relative to its placement in the blockchain. Minimum " + std::to_string(HTLC_MINIMUM_BLOCK_COUNT) + "."},
            {"seed", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "SHA256 hash of the seed. If none provided one will be generated"},
        },
        RPCResult{
            "{\n"
            "  \"address\":\"address\"       (string) The value of the new Bitcoin address\n"
            "  \"redeemScript\":\"script\"   (string) Hex-encoded redemption script\n"
            "  \"seed\":\"seed\"             (string) Hex-encoded seed if no seed provided\n"
            "  \"seedhash\":\"seedhash\"     (string) Hex-encoded seed hash if no seed provided\n"
            "}\n"
        },
        RPCExamples{
            HelpExampleCli("spv_createhtlc", "0333ffc4d18c7b2adbd1df49f5486030b0b70449c421189c2c0f8981d0da9669af 034201385acc094d24db4b53a05fc8991b10e3467e6e20a8551c49f89e7e4d0d3c 10 254e38932fdb9fc27f82aac2a5cc6d789664832383e3cf3298f8c120812712db")
            + HelpExampleRpc("spv_createhtlc", "0333ffc4d18c7b2adbd1df49f5486030b0b70449c421189c2c0f8981d0da9669af, 034201385acc094d24db4b53a05fc8991b10e3467e6e20a8551c49f89e7e4d0d3c, 10, 254e38932fdb9fc27f82aac2a5cc6d789664832383e3cf3298f8c120812712db")
        },
    }.Check(request);

    if (!spv::pspv)
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    // Check that we are connected
    if (!spv::pspv->IsConnected()) {
        throw JSONRPCError(RPC_MISC_ERROR, "spv not connected");
    }

    // Make sure we are fully synced
    if (spv::pspv->GetLastBlockHeight() < spv::pspv->GetEstimatedBlockHeight()) {
        auto blocksRemaining = std::to_string(spv::pspv->GetEstimatedBlockHeight() -spv::pspv->GetLastBlockHeight());
        throw JSONRPCError(RPC_MISC_ERROR, "spv still syncing, " + blocksRemaining + " blocks left.");
    }

    std::vector<unsigned char> hashBytes;
    CKeyingMaterial seed;

    // Seed hash provided
    if (!request.params[3].isNull())
    {
        std::string hash = request.params[3].get_str();

        if (IsHex(hash))
        {
            hashBytes = ParseHex(hash);

            if (hashBytes.size() != 32)
            {
                throw JSONRPCError(RPC_TYPE_ERROR, "Invalid hash image length, 32 (SHA256) accepted");
            }
        }
        else
        {
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid hash image");
        }
    }
    else // No seed hash provided, generate seed
    {
        hashBytes.resize(32);
        seed.resize(32);
        GetStrongRandBytes(seed.data(), seed.size());

        CSHA256 hash;
        hash.Write(seed.data(), seed.size());
        hash.Finalize(hashBytes.data());
    }

    // Get HTLC script
    uint32_t blocks;
    CScript inner = CreateScriptForHTLC(request, blocks, hashBytes);

    // Get destination
    CScriptID innerID(inner);
    ScriptHash scriptHash(innerID);

    // Add address and script to DeFi wallet storage for persistance
    pwallet->SetAddressBook(scriptHash, "htlc", "htlc");
    pwallet->AddCScript(inner);

    // Add to SPV to watch transactions to this script
    spv::pspv->AddBitcoinHash(scriptHash, true);
    spv::pspv->RebuildBloomFilter(true);

    // Create Bitcoin address
    std::vector<unsigned char> data(21, spv::pspv->GetP2SHPrefix());
    memcpy(&data[1], &innerID, 20);

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", EncodeBase58Check(data));
    result.pushKV("redeemScript", HexStr(inner));

    if (!seed.empty())
    {
        result.pushKV("seed", HexStr(seed));
        result.pushKV("seedhash", HexStr(hashBytes));
    }

    return result;
}

UniValue spv_listhtlcoutputs(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_listhtlcoutputs",
        "\nList all outputs related to HTLC addresses in the wallet\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "HTLC address to filter results"},
        },
        RPCResult{
            "[                       (JSON array of transaction details)\n"
            "{\n"
            "  \"txid\"              (string) The transaction id\n"
            "  \"vout\"              (numeric) Output relating to the HTLC address\n"
            "  \"address\"           (string) HTLC address\n"
            "  \"confirms\"          (numeric) Number of confirmations\n"
            "  { \"spent\"           (JSON object containing spent info)\n"
            "    \"txid\"            (string) Transaction id spending this output\n"
            "    \"confirms\"        (numeric) Number of spent confirmations\n"
            "  }\n"
            "}, ...]\n"
        },
        RPCExamples{
            HelpExampleCli("spv_listhtlcoutputs", "")
            + HelpExampleRpc("spv_listhtlcoutputs", "")
        },
    }.Check(request);

    if (!spv::pspv)
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    return spv::pspv->GetHTLCReceived(request.params[0].isNull() ? "" : request.params[0].get_str());
}

UniValue spv_gethtlcseed(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_gethtlcseed",
        "\nReturns the HTLC secret if available\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "HTLC address"},
        },
        RPCResult{
            "\"secret\"                 (string) Returns HTLC seed\n"
        },
        RPCExamples{
            HelpExampleCli("spv_gethtlcseed", "\\\"address\\\"")
            + HelpExampleRpc("spv_gethtlcseed", "\\\"address\\\"")
        },
    }.Check(request);

    if (!spv::pspv)
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    CKeyID key = spv::pspv->GetAddressKeyID(request.params[0].get_str().c_str());

    return spv::pspv->GetHTLCSeed(key.begin());
}

UniValue spv_claimhtlc(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_claimhtlc",
        "\nClaims all coins in HTLC address\n",
        {
            {"scriptaddress", RPCArg::Type::STR, RPCArg::Optional::NO, "HTLC address"},
            {"destinationaddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Destination for funds in the HTLC"},
            {"seed", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Seed that was used to generate the hash in the HTLC"},
            {"feerate", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Feerate (satoshis) per KB (Default: " + std::to_string(spv::DEFAULT_BTC_FEE_PER_KB) + ")"},
        },
        RPCResult{
            "{\n"
            "  \"txid\"                    (string) The transaction id\n"
            "  \"sendmessage\"             (string) Error message on failure\n"
            "}\n"
        },
        RPCExamples{
            HelpExampleCli("spv_claimhtlc", "\"3QwKW5GKHc1eSbbwTozsVzB1UBVyAbZQpa\" \"bc1q28jh8l7a9m0x5ngq0ccld2glpn4ehzwmfczf0n\" \"696c6c756d696e617469\" 100000")
            + HelpExampleRpc("spv_claimhtlc", "\"3QwKW5GKHc1eSbbwTozsVzB1UBVyAbZQpa\", \"bc1q28jh8l7a9m0x5ngq0ccld2glpn4ehzwmfczf0n\", \"696c6c756d696e617469\", 100000")
        },
    }.Check(request);

    if (!spv::pspv)
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    if (!spv::pspv->IsConnected())
    {
        throw JSONRPCError(RPC_MISC_ERROR, "spv not connected");
    }

    const auto pair = spv::pspv->PrepareHTLCTransaction(pwallet, request.params[0].get_str().c_str(), request.params[1].get_str().c_str(),
            request.params[2].get_str(), request.params[3].isNull() ? spv::DEFAULT_BTC_FEE_PER_KB : request.params[3].get_int64(), true);

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", pair.first);
    result.pushKV("sendmessage", pair.second);
    return result;
}

UniValue spv_refundhtlc(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_refundhtlc",
        "\nRefunds all coins in HTLC address\n",
        {
            {"scriptaddress", RPCArg::Type::STR, RPCArg::Optional::NO, "HTLC address"},
            {"destinationaddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Destination for funds in the HTLC"},
            {"feerate", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Feerate (satoshis) per KB (Default: " + std::to_string(spv::DEFAULT_BTC_FEE_PER_KB) + ")"},
        },
        RPCResult{
            "{\n"
            "  \"txid\"                    (string) The transaction id\n"
            "  \"sendmessage\"             (string) Error message on failure\n"
            "}\n"
        },
        RPCExamples{
            HelpExampleCli("spv_refundhtlc", "\"3QwKW5GKHc1eSbbwTozsVzB1UBVyAbZQpa\" \"bc1q28jh8l7a9m0x5ngq0ccld2glpn4ehzwmfczf0n\" 100000")
            + HelpExampleRpc("spv_refundhtlc", "\"3QwKW5GKHc1eSbbwTozsVzB1UBVyAbZQpa\", \"bc1q28jh8l7a9m0x5ngq0ccld2glpn4ehzwmfczf0n\", 100000")
        },
    }.Check(request);

    if (!spv::pspv)
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    if (!spv::pspv->IsConnected())
    {
        throw JSONRPCError(RPC_MISC_ERROR, "spv not connected");
    }

    const auto pair = spv::pspv->PrepareHTLCTransaction(pwallet, request.params[0].get_str().c_str(), request.params[1].get_str().c_str(),
            "", request.params[3].isNull() ? spv::DEFAULT_BTC_FEE_PER_KB : request.params[3].get_int64(), false);

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", pair.first);
    result.pushKV("sendmessage", pair.second);
    return result;
}

UniValue spv_refundhtlcall(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_refundhtlcall",
               "\nGets all HTLC contracts stored in wallet and creates refunds transactions for all that have expired\n",
               {
                   {"destinationaddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Destination for funds in the HTLC"},
                   {"feerate", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Feerate (satoshis) per KB (Default: " + std::to_string(spv::DEFAULT_BTC_FEE_PER_KB) + ")"},
               },
               RPCResult{
                   "{\n"
                   "  \"txid\"                    (string) The transaction id\n"
                   "}\n"
               },
               RPCExamples{
        HelpExampleCli("spv_refundhtlcall", "100000")
        + HelpExampleRpc("spv_refundhtlcall", "100000")
               },
               }.Check(request);

    if (!spv::pspv)
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    if (!spv::pspv->IsConnected())
    {
        throw JSONRPCError(RPC_MISC_ERROR, "spv not connected");
    }

    const auto feeRate = request.params[1].isNull() ? spv::DEFAULT_BTC_FEE_PER_KB : request.params[1].get_int64();

    return spv::pspv->RefundAllHTLC(pwallet, request.params[0].get_str().c_str(), feeRate);
}

UniValue spv_fundaddress(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_fundaddress",
        "\nFund a Bitcoin address (for test purposes only)\n",
        {
            {"address", RPCArg::Type::NUM, RPCArg::Optional::NO, "Bitcoin address to fund"},
        },
        RPCResult{
            "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
        },
        RPCExamples{
            HelpExampleCli("spv_fundaddress", "\"address\"")
            + HelpExampleRpc("spv_fundaddress", "\"address\"")
        },
    }.Check(request);

    auto fake_spv = static_cast<spv::CFakeSpvWrapper *>(spv::pspv.get());
    if (!fake_spv) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "command disabled");
    }

    std::string strAddress = request.params[0].get_str();

    return fake_spv->SendBitcoins(pwallet, strAddress, -1, 1000);
}

static UniValue spv_getnewaddress(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_getnewaddress",
        "\nCreates and adds a Bitcoin address to the SPV wallet\n",
        {
        },
        RPCResult{
            "\"address\"                  Returns a new Bitcoin address\n"
        },
        RPCExamples{
            HelpExampleCli("spv_getnewaddress", "")
            + HelpExampleRpc("spv_getnewaddress", "")
        },
    }.Check(request);

    if (!spv::pspv) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    LOCK(pwallet->cs_wallet);

    pwallet->TopUpKeyPool();

    // Generate a new key that is added to wallet
    CPubKey new_key;
    if (!pwallet->GetKeyFromPool(new_key)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    }

    auto newAddress = spv::pspv->AddBitcoinAddress(new_key);
    if (!newAddress.empty()) {
        auto dest = GetDestinationForKey(new_key, OutputType::BECH32);
        pwallet->SetAddressBook(dest, "spv", "spv");
    }

    return newAddress;
}

static UniValue spv_getaddresspubkey(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_getaddresspubkey",
        "\nReturn raw pubkey for Bitcoin address if in SPV wallet\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "Bitcoin address"},
        },
        RPCResult{
            "\"pubkey\"                 (string) Raw pubkey hex\n"
        },
        RPCExamples{
            HelpExampleCli("spv_getaddresspubkey", "")
            + HelpExampleRpc("spv_getaddresspubkey", "")
        },
    }.Check(request);

    if (!spv::pspv) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    LOCK(pwallet->cs_wallet);

    auto address = request.params[0].get_str().c_str();

    return spv::pspv->GetAddressPubkey(pwallet, address);
}

static UniValue spv_dumpprivkey(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_dumpprivkey",
        "\nReveals the private key corresponding to 'address'\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The BTC address for the private key"},
        },
        RPCResult{
            "\"key\"                (string) The private key\n"
        },
        RPCExamples{
            HelpExampleCli("spv_dumpprivkey", "\"myaddress\"")
            + HelpExampleRpc("spv_dumpprivkey", "\"myaddress\"")
        },
    }.Check(request);

    auto locked_chain = pwallet->chain().lock();
    LOCK2(pwallet->cs_wallet, locked_chain->mutex());

    EnsureWalletIsUnlocked(pwallet);

    std::string strAddress = request.params[0].get_str();

    return spv::pspv->DumpBitcoinPrivKey(pwallet, strAddress);
}

static UniValue spv_getbalance(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_getbalance",
        "\nReturns the Bitcoin balance of the SPV wallet\n",
        {
        },
        RPCResult{
            "amount                 (numeric) The total amount in BTC received in the SPV wallet.\n"
        },
        RPCExamples{
            HelpExampleCli("spv_getbalance", "")
            + HelpExampleRpc("spv_getbalance", "")
        },
    }.Check(request);

    return ValueFromAmount(spv::pspv->GetBitcoinBalance());
}


static UniValue spv_sendtoaddress(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    RPCHelpMan{"spv_sendtoaddress",
        "\nSend a Bitcoin amount to a given address." +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The Bitcoin address to send to."},
            {"amount", RPCArg::Type::AMOUNT, RPCArg::Optional::NO, "The amount in BTC to send. eg 0.1"},
            {"feerate", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Feerate (satoshis) per KB (Default: " + std::to_string(spv::DEFAULT_BTC_FEE_PER_KB) + ")"},
        },
        RPCResult{
            "{\n"
            "  \"txid\"                    (string) The transaction id\n"
            "  \"sendmessage\"             (string) Send message result\n"
            "}\n"
        },
        RPCExamples{
            HelpExampleCli("spv_sendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\" 0.1")
            + HelpExampleRpc("spv_sendtoaddress", "\"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\", 0.1")
        },
    }.Check(request);

    if (!spv::pspv) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    if (!spv::pspv->IsConnected()) {
        throw JSONRPCError(RPC_MISC_ERROR, "spv not connected");
    }

    uint32_t bitcoinBlocksDay{140};
    if (spv::pspv->GetLastBlockHeight() + bitcoinBlocksDay < spv::pspv->GetEstimatedBlockHeight()) {
        auto blocksRemaining = std::to_string(spv::pspv->GetEstimatedBlockHeight() -spv::pspv->GetLastBlockHeight());
        throw JSONRPCError(RPC_MISC_ERROR, "spv still syncing, " + blocksRemaining + " blocks left.");
    }

    auto locked_chain = pwallet->chain().lock();
    LOCK2(pwallet->cs_wallet, locked_chain->mutex());

    std::string address = request.params[0].get_str();

    // Amount
    CAmount nAmount = AmountFromValue(request.params[1]);
    if (nAmount <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");
    }

    EnsureWalletIsUnlocked(pwallet);

    uint64_t feerate = request.params[2].isNull() ? spv::DEFAULT_BTC_FEE_PER_KB : request.params[2].get_int64();
    if (feerate < spv::DEFAULT_BTC_FEERATE)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Fee size below minimum acceptable amount");
    }

    return spv::pspv->SendBitcoins(pwallet, address, nAmount, feerate);
}

static UniValue spv_listtransactions(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_listtransactions",
        "\nReturns an array of all Bitcoin transaction hashes.\n",
        {},
        RPCResult{
            "[                         (array of strings)\n"
            "  \"txid\"                  (string) The transaction id.\n"
            "  ...\n"
            "]"
        },
        RPCExamples{
            HelpExampleCli("spv_listtransactions", "")
            + HelpExampleRpc("spv_listtransactions", "")
        },
    }.Check(request);

    if (!spv::pspv) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    return spv::pspv->ListTransactions();
}

static UniValue spv_getrawtransaction(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_getrawtransaction",
        "\nReturn the raw transaction data.\n",
        {
            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
        },
        RPCResult{
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"
        },
        RPCExamples{
            HelpExampleCli("spv_getrawtransaction", "\"txid\"")
            + HelpExampleRpc("spv_getrawtransaction", "\"txid\"")
        },
    }.Check(request);

    if (!spv::pspv) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    uint256 hash = ParseHashV(request.params[0], "");

    return spv::pspv->GetRawTransactions(hash);
}

static UniValue spv_listreceivedbyaddress(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_listreceivedbyaddress",
        "\nList balances by receiving address.\n",
        {
            {"minconf", RPCArg::Type::NUM, /* default */ "1", "The minimum number of confirmations before payments are included."},
            {"address_filter", RPCArg::Type::STR, RPCArg::Optional::OMITTED_NAMED_ARG, "If present, only return information on this address."},
        },
        RPCResult{
            "[\n"
            "  {\n"
            "    \"address\" : \"receivingaddress\",  (string) The receiving address\n"
            "    \"type\" : \"type\",                 (string) Address type, Bech32 or HTLC\n"
            "    \"amount\" : x.xxx,                  (numeric) The total amount in BTC received by the address\n"
            "    \"confirmations\" : n,               (numeric) The number of confirmations of the most recent transaction included\n"
            "    \"txids\": [\n"
            "       \"txid\",                         (string) The ids of transactions received with the address \n"
            "       ...\n"
            "    ]\n"
            "  }\n"
            "  ,...\n"
            "]\n"
        },
        RPCExamples{
            HelpExampleCli("spv_listreceivedbyaddress", "")
            + HelpExampleCli("spv_listreceivedbyaddress", "6")
            + HelpExampleRpc("spv_listreceivedbyaddress", "6")
            + HelpExampleRpc("spv_listreceivedbyaddress", "6, \"1M72Sfpbz1BPpXFHz9m3CdqATR44Jvaydd\"")
        },
    }.Check(request);

    if (!spv::pspv)
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    // Minimum confirmations
    int nMinDepth = 1;
    if (!request.params[0].isNull())
    {
        nMinDepth = request.params[0].get_int();
    }

    // Address filter
    std::string address;
    if (!request.params[1].isNull())
    {
        address = request.params[1].get_str();
    }

    return spv::pspv->ListReceived(nMinDepth, address);
}

static UniValue spv_validateaddress(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_validateaddress",
        "\nCheck whether the given Bitcoin address is valid.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The Bitcoin address to validate"},
        },
        RPCResult{
            "{\n"
            "  \"isvalid\" : true|false,       (boolean) If the address is valid or not.\n"
            "  \"ismine\" : true|false,        (boolean) If the address belongs to the wallet.\n"
            "}\n"
        },
        RPCExamples{
            HelpExampleCli("spv_validateaddress", "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"")
            + HelpExampleRpc("spv_validateaddress", "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"")
        },
    }.Check(request);

    if (!spv::pspv)
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    return spv::pspv->ValidateAddress(request.params[0].get_str().c_str());
}

static UniValue spv_getalladdresses(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_getalladdresses",
        "\nReturns all user Bitcoin addresses.\n",
        {
        },
        RPCResult{
            "\"array\"                  (Array of user addresses)\n"
        },
        RPCExamples{
            HelpExampleCli("spv_getalladdresses", "")
            + HelpExampleRpc("spv_getalladdresses", "")
        },
    }.Check(request);

    if (!spv::pspv)
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    return spv::pspv->GetAllAddress();
}

static UniValue spv_getfeerate(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_getfeerate",
               "\nReturns current fee rate in Sats per KB.\n",
               {
               },
               RPCResult{
                       "nnnn                  (Fee rate)\n"
               },
               RPCExamples{
                       HelpExampleCli("spv_getfeerate", "")
                       + HelpExampleRpc("spv_getfeerate", "")
               },
    }.Check(request);

    if (!spv::pspv)
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    return spv::pspv->GetFeeRate();
}

static UniValue spv_getpeers(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_getpeers",
               "\nReturns info on connected peers.\n",
               {
               },
               RPCResult{
                 "X {                                (Peer number)\n"
                       "  address: xxx.xxx.xxx.xxx          (IP Address)\n"
                       "  timestamp: nnn                    (time)\n"
                       "  flags: nnn                        (flags)\n"
                       "  services: nnn                     (services)\n"
                       "}\n"
               },
               RPCExamples{
                       HelpExampleCli("spv_getpeers", "")
                       + HelpExampleRpc("spv_getpeers", "")
               },
    }.Check(request);

    if (!spv::pspv)
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");
    }

    return spv::pspv->GetPeers();
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
  { "spv",      "spv_listanchors",            &spv_listanchors,           { "minBtcHeight", "maxBtcHeight", "minConfs", "maxConfs", "startBtcHeight", "limit" }  },
  { "spv",      "spv_listanchorauths",        &spv_listanchorauths,       { }  },
  { "spv",      "spv_listanchorrewardconfirms",     &spv_listanchorrewardconfirms,    { }  },
  { "spv",      "spv_listanchorrewards",      &spv_listanchorrewards,     { }  },
  { "spv",      "spv_listanchorsunrewarded",  &spv_listanchorsunrewarded, { }  },
  { "spv",      "spv_listanchorspending",     &spv_listanchorspending,    { }  },
  { "spv",      "spv_getnewaddress",          &spv_getnewaddress,         { }  },
  { "spv",      "spv_getaddresspubkey",       &spv_getaddresspubkey,      { "address" }  },
  { "spv",      "spv_dumpprivkey",            &spv_dumpprivkey,           { }  },
  { "spv",      "spv_getbalance",             &spv_getbalance,            { }  },
  { "spv",      "spv_sendtoaddress",          &spv_sendtoaddress,         { "address", "amount", "feerate" }  },
  { "spv",      "spv_listtransactions",       &spv_listtransactions,      { }  },
  { "spv",      "spv_getrawtransaction",      &spv_getrawtransaction,     { "txid" }  },
  { "spv",      "spv_createhtlc",             &spv_createhtlc,            { "seller_key", "refund_key", "hash", "timeout" }  },
  { "spv",      "spv_claimhtlc",              &spv_claimhtlc,             { "scriptaddress", "destinationaddress", "seed", "feerate" }  },
  { "spv",      "spv_refundhtlc",             &spv_refundhtlc,            { "scriptaddress", "destinationaddress", "feerate" }  },
  { "spv",      "spv_refundhtlcall",          &spv_refundhtlcall,         { "destinationaddress", "feerate" }  },
  { "spv",      "spv_listhtlcoutputs",        &spv_listhtlcoutputs,       { "address" }  },
  { "spv",      "spv_decodehtlcscript",       &spv_decodehtlcscript,      { "redeemscript" }  },
  { "spv",      "spv_gethtlcseed",            &spv_gethtlcseed,           { "address" }  },
  { "spv",      "spv_listreceivedbyaddress",  &spv_listreceivedbyaddress, { "minconf", "address_filter" }  },
  { "spv",      "spv_validateaddress",        &spv_validateaddress,       { "address"}  },
  { "spv",      "spv_getalladdresses",        &spv_getalladdresses,       { }  },
  { "spv",      "spv_getfeerate",             &spv_getfeerate,            { }  },
  { "spv",      "spv_getpeers",               &spv_getpeers,              { }  },
  { "hidden",   "spv_setlastheight",          &spv_setlastheight,         { "height" }  },
  { "hidden",   "spv_fundaddress",            &spv_fundaddress,           { "address" }  },
};

void RegisterSpvRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
