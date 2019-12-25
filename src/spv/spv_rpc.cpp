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
#include <spv/spv_wrapper.h>
#include <univalue/include/univalue.h>

//#ifdef ENABLE_WALLET
#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>
//#endif

#include <stdexcept>

static CWallet* GetWallet(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    EnsureWalletIsAvailable(pwallet, false);
    EnsureWalletIsUnlocked(pwallet);
    return pwallet;
}

CAnchor createAnchorMessage(CTxDestination const & rewardDest, uint256 const & forBlock = uint256())
{
    CAnchor const anchor = panchorauths->CreateBestAnchor(rewardDest, forBlock);

    /// @todo @maxb conform to "defi" current team? (from last finalize tx)
    auto minQuorum = GetMinAnchorQuorum(panchors->GetCurrentTeam(panchors->GetActiveAnchor()));
    if (anchor.sigs.size() < minQuorum) {
        throw JSONRPCError(RPC_VERIFY_ERROR, "Min anchor quorum was not reached (" + std::to_string(anchor.sigs.size()) + ", need "+ std::to_string(minQuorum) + ") ");
    }
    return anchor;
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

    spv::pspv->SendRawTx(ParseHexV(request.params[0], "rawtx"));
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

    auto locked_chain = pwallet->chain().lock();

    /// @todo @maxb temporary, tests
    auto rawtx = spv::CreateSplitTx("1251d1fc46d104564ca8311696d561bf7de5c0e336039c7ccfe103f7cdfc026e", 2, 3071995, "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP", parts, amount);

    bool send = false;
    if (send) {
        if (!spv::pspv)
            throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");

        spv::pspv->SendRawTx(rawtx);
    }

    CMutableTransaction mtx;
    /// @todo @maxb implement separated bitcoin serialize/deserialize
    DecodeHexTx(mtx, std::string(rawtx.begin(), rawtx.end()), true);

    UniValue result(UniValue::VOBJ);
    result.pushKV("txHex", HexStr(rawtx));
    /// @attention WRONG HASH!!!
    result.pushKV("txHash", CTransaction(mtx).GetHash().ToString());

    return result;
}


/*
 * Create, sign and send (optional) anchor tx using only spv api
 * Issued by: any
*/
UniValue spv_createanchor(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_createanchor",
        "\nCreates (and optional submits to bitcoin blockchain) an anchor tx with given OR latest possible (every 15th) authorized blockhash.\n"
        "The first argument is the specific UTXOs to spend." +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
                /// @todo @maxb not fully implemented yet! now in test mode!
//            {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "A json array of json objects",
//                {
//                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "", /// @todo @maxb change to 'NO'
//                        {
//                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
//                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
//                            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount of output"},
//                            {"privkey", RPCArg::Type::STR, RPCArg::Optional::NO, "WIF private key for signing this output"},
//                        },
//                    },
//                },
//            },
            {"rewardAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "User's P2PKH address (in DeFi chain) for reward"},
            {"hash", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "ID of block in DF chain to anchor to. Latest possible authorized block if omitted." },
        },
        RPCResult{
            "\"txHex\"                  (string) The hex-encoded raw transaction with signature(s)\n"
            "\"txHash\"                 (string) The hex-encoded transaction hash\n"
        },
        RPCExamples{
            HelpExampleCli("spv_createanchor", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0,\\\"amount\\\":10000,\\\"privkey\\\":\\\"WIFprivkey\\\"}]\" "
                                            "\\\"rewardAddress\\\" \\\"blockHash\\\""
                                            )
            + HelpExampleRpc("spv_createanchor", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0,\\\"amount\\\":10000,\\\"privkey\\\":\\\"WIFprivkey\\\"}]\" "
                                                 "\\\"rewardAddress\\\" \\\"blockHash\\\""
                                                 )
        },
    }.Check(request);


    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create anchor while still in Initial Block Download");
    }

//    RPCTypeCheck(request.params, { UniValue::VARR, UniValue::VSTR, UniValue::VSTR }, true);
//    if (request.params[0].isNull() || request.params[1].isNull())
//    {
//        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 and 2 must be non-null");
//    }
//    std::string rewardAddress = request.params[1].getValStr();
//    CTxDestination rewardDest = DecodeDestination(rewardAddress);
//    if (rewardDest.which() != 1 && rewardDest.which() != 4)
//    {
//        throw JSONRPCError(RPC_INVALID_PARAMETER, "rewardAddress (" + rewardAddress + ") does not refer to a P2PKH or P2WPKH address");
//    }
//    uint256 const forBlock(ParseHashV(request.params[2].getValStr(), "blockHash"));


    auto locked_chain = pwallet->chain().lock();

    /// @todo @maxb temporary, tests

    CTxDestination rewardDest = DecodeDestination("mmjrUWSKQqnkWzyS98GCuFxA7TXcK3bc3A");
    CAnchor const anchor = panchorauths->CreateBestAnchor(rewardDest/*, forBlock*/);
    if (anchor.sigs.empty()) {
        throw JSONRPCError(RPC_VERIFY_ERROR, "Min anchor quorum was not reached!");
    }

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << anchor;

    /// @todo @maxb temporary, tests
//    auto rawtx = spv::CreateAnchorTx("e6f0a5e4db120f6877710bbbb5f9523162b6456bb1d4d89b854e60a794e03b46", 1, 3271995, "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP", ToByteVector(ss));
    auto rawtx = spv::CreateAnchorTx("a0d5a294be3cde6a8bddab5815b8c4cb1b2ebf2c2b8a4018205d6f8c576e8963", 3, 2262303, "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP", ToByteVector(ss));

    bool send = false;
    if (send) {
        if (!spv::pspv)
            throw JSONRPCError(RPC_INVALID_REQUEST, "spv module disabled");

        spv::pspv->SendRawTx(rawtx);
    }

    CMutableTransaction mtx;
    /// @todo @maxb implement separated bitcoin serialize/deserialize
    DecodeHexTx(mtx, std::string(rawtx.begin(), rawtx.end()), true);

    UniValue result(UniValue::VOBJ);
//    result.pushKV("anchorMsg", HexStr(ss.begin(), ss.end()));
//    result.pushKV("anchorMsgHash", anchor.GetHash().ToString());
    result.pushKV("txHex", HexStr(rawtx));
    /// @attention WRONG HASH!!!
    result.pushKV("txHash", CTransaction(mtx).GetHash().ToString());

    return result;
}

/// @todo @maxb not fully implemented yet! now in test mode!
UniValue spv_createanchortemplate(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_createanchortemplate",
        "\nCreates an anchor tx template with given OR latest possible (every 15th) authorized blockhash.\n" +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
            {"rewardAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "User's P2PKH address (in DeFi chain) for reward"},
            {"hash", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "ID of block in DF chain to anchor to. Latest possible authorized block if omitted." },
        },
        RPCResult{
            "\"txHex\"                  (string) The hex-encoded raw transaction with signature(s)\n"
            "\"txHash\"                 (string) The hex-encoded transaction hash\n"
        },
        RPCExamples{
            HelpExampleCli("spv_createanchortemplate", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0,\\\"amount\\\":10000,\\\"privkey\\\":\\\"WIFprivkey\\\"}]\" "
                                            "\\\"rewardAddress\\\" \\\"blockHash\\\""
                                            )
            + HelpExampleRpc("spv_createanchortemplate", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0,\\\"amount\\\":10000,\\\"privkey\\\":\\\"WIFprivkey\\\"}]\" "
                                                 "\\\"rewardAddress\\\" \\\"blockHash\\\""
                                                 )
        },
    }.Check(request);


    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create anchor while still in Initial Block Download");
    }

    std::string rewardAddress = request.params[0].getValStr();
    CTxDestination rewardDest = DecodeDestination(rewardAddress);
    if (rewardDest.which() != 1 && rewardDest.which() != 4)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "rewardAddress (" + rewardAddress + ") does not refer to a P2PKH or P2WPKH address");
    }

    uint256 const hash = request.params[1].isNull() ? uint256() : ParseHashV(request.params[1], "hash");

    auto locked_chain = pwallet->chain().lock();

    CAnchor const anchor = panchorauths->CreateBestAnchor(rewardDest/*, forBlock*/);
    if (anchor.sigs.empty()) {
        throw JSONRPCError(RPC_VERIFY_ERROR, "Min anchor quorum was not reached!");
    }

    /// @todo @maxb will be implemented only after anchors tests

//    CScript scriptMeta = CScript() << OP_RETURN << spv::BtcAnchorMarker << ToByteVector(anchor.GetHash());

//    CMutableTransaction rawTx;
//    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    // "manually" decode anchor address and construct script;
//    uint160 anchorPKHash;
//    {
//        std::vector<unsigned char> data;
//        if (DecodeBase58Check(Params().GetConsensus().spv.anchors_address, data)) {
//            // base58-encoded Bitcoin addresses.
//            // Public-key-hash-addresses have version 0 (or 111 testnet).
//            // The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is the serialized public key.
//            const std::vector<unsigned char> pubkey_prefix = { spv::pspv->GetPKHashPrefix() };
//            if (data.size() == anchorPKHash.size() + pubkey_prefix.size() && std::equal(pubkey_prefix.begin(), pubkey_prefix.end(), data.begin())) {
//                std::copy(data.begin() + pubkey_prefix.size(), data.end(), anchorPKHash.begin());
//            }
//        }
//    }
//    CScript const anchorScript = CScript() << OP_DUP << OP_HASH160 << ToByteVector(anchorPKHash) << OP_EQUALVERIFY << OP_CHECKSIG;


//    spv::TBytes const rawscript(spv::CreateScriptForAddress(Params().GetConsensus().spv.anchors_address));
//    // This should not happen or spv.anchors_address is WRONG!
//    assert (rawscript.size() != 0);
//    rawTx.vout.push_back(CTxOut(Params().GetConsensus().spv.creationFee, CScript(rawscript.begin(), rawscript.end())));

//    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
//    ss << anchor;

    UniValue result(UniValue::VOBJ);
//    result.pushKV("anchorMsg", HexStr(ss.begin(), ss.end()));
//    result.pushKV("anchorMsgHash", anchor.GetHash().ToString());
//    result.pushKV("txHex", EncodeHexTx(CTransaction(rawTx)));
//    result.pushKV("txHash", CTransaction(rawTx).GetHash().ToString());
    return result;
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
//    result.pushKV("txCount", static_cast<int>(spv::pspv->GetWalletTxs().size()));
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

    auto locked_chain = pwallet->chain().lock();

    return UniValue(panchors->GetAnchorConfirmations(txHash));
//    return UniValue(spv::pspv->GetTxConfirmations(txHash));
}


static const CRPCCommand commands[] =
{ //  category          name                        actor (function)            params
  //  ----------------- ------------------------    -----------------------     ----------
  { "spv",      "spv_sendrawtx",              &spv_sendrawtx,             { "rawtx" }  },
  { "spv",      "spv_createanchor",           &spv_createanchor,          { /*"inputs", */ "rewardAddress", "hash" }  },
  { "spv",      "spv_createanchortemplate",   &spv_createanchortemplate,  { /*"inputs", */ "rewardAddress", "hash" }  },
  { "spv",      "spv_rescan",                 &spv_rescan,                { "height" }  },
  { "spv",      "spv_syncstatus",             &spv_syncstatus,            { }  },
  { "spv",      "spv_gettxconfirmations",     &spv_gettxconfirmations,    { "txhash" }  },
  { "spv",      "spv_splitutxo",              &spv_splitutxo,             { "parts", "amount" }  },

};

void RegisterSpvRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
