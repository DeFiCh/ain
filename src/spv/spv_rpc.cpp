// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <chainparams.h>
#include <core_io.h>
//#include <consensus/validation.h>
//#include <net.h>
#include <rpc/client.h>
#include <rpc/server.h>
#include <rpc/protocol.h>
#include <rpc/util.h>
#include <masternodes/anchors.h>
#include <masternodes/masternodes.h>
#include <net_processing.h>
#include <spv/spv_wrapper.h>
//#include <script/script_error.h>
//#include <script/sign.h>
#include <univalue/include/univalue.h>
//#include <util/validation.h>
//#include <validation.h>
//#include <version.h>

//#include <spv/bitcoin/BRChainParams.h> // do not include it!
//#include <spv/support/BRLargeInt.h>
//#include <spv/support/BRKey.h>
//#include <spv/support/BRAddress.h>
//#include <spv/support/BRBIP39Mnemonic.h>
//#include <spv/support/BRBIP32Sequence.h>
//#include <spv/bitcoin/BRPeerManager.h>
//#include <spv/bitcoin/BRChainParams.h>
//#include <spv/bcash/BRBCashParams.h>


//#ifdef ENABLE_WALLET
#include <wallet/coincontrol.h>
#include <wallet/rpcwallet.h>
#include <wallet/wallet.h>
//#endif

#include <future>
#include <stdexcept>

#include <boost/algorithm/string.hpp>
#include <boost/assign/list_of.hpp>


//extern void ScriptPubKeyToJSON(CScript const & scriptPubKey, UniValue & out, bool fIncludeHex); // in rawtransaction.cpp

static CWallet* GetWallet(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    EnsureWalletIsAvailable(pwallet, false);
    EnsureWalletIsUnlocked(pwallet);
    return pwallet;
}

CAnchorMessage createAnchorMessage(std::string const & rewardAddress, uint256 const & forBlock = uint256())
{
    spv::TBytes rawscript(spv::CreateScriptForAddress(rewardAddress));
    if (rawscript.size() == 0) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Wrong reward address: " + rewardAddress);
    }
    CAnchorMessage const anchor = panchorauths->CreateBestAnchor(forBlock, CScript(rawscript.begin(), rawscript.end()));

    auto minQuorum = GetMinAnchorQuorum(panchors->GetCurrentTeam(panchors->GetActiveAnchor()));
    if (anchor.sigs.size() < minQuorum) {
        throw JSONRPCError(RPC_VERIFY_ERROR, "Min anchor quorum was not reached (" + std::to_string(anchor.sigs.size()) + ", need "+ std::to_string(minQuorum) + ") ");
    }
    return anchor;
}

UniValue spv_sendrawtx(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_sendrawtx",
        "\nSending anchor raw tx to botcoin blockchain\n",
        {
            {"rawtx", RPCArg::Type::STR, RPCArg::Optional::NO, "The hex-encoded raw transaction with signature" },
        },
        RPCResult{
            "\"none\"                  Always successful\n"
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

    spv::pspv->SendRawTx(ParseHexV(request.params[0], "rawtx"));
    return UniValue("");
}

/*
 * Create, sign and send (optional) anchor tx using only spv api
 * Issued by: any
*/
UniValue spv_createanchor(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_createanchor",
        "\nCreates (and submits to local node and network) a masternode creation transaction with given metadata, spending the given inputs..\n"
        "The first optional argument (may be empty array) is an array of specific UTXOs to spend." +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
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
//            {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
//                {
//                    {"hash", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "ID of block in DF chain to anchor to. Current ChaiTip if omitted." },
//                    {"rewardAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "User's P2PKH address for reward"},
//                },
//            },
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
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create anchor while still in Initial Block Download");
    }

//    RPCTypeCheck(request.params, { UniValue::VARR, UniValue::VOBJ }, true);
//    if (request.params[0].isNull() || request.params[1].isNull())
//    {
//        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 and 2 must be non-null, and argument 2 expected as object with "
//                                                  "{\"hash\",\"rewardAddress\"}");
//    }
//    UniValue metaObj = request.params[1].get_obj();
//    RPCTypeCheckObj(metaObj, {
//                        { "hash", UniValue::VSTR },
//                        { "rewardAddress", UniValue::VSTR }
//                    },
//                    true, true);

//    uint256 const hash(ParseHashV(metaObj["hash"], "hash"));
//    std::string rewardAddress = metaObj["rewardAddress"].getValStr();

    auto locked_chain = pwallet->chain().lock();

    /// @todo @maxb temporary, tests
    CAnchorMessage const anchor = createAnchorMessage("mmjrUWSKQqnkWzyS98GCuFxA7TXcK3bc3A");
    //    CAnchorMessage const anchor = createAnchorMessage(rewardAddress, hash);
    CScript scriptMeta = CScript() << OP_RETURN << spv::BtcAnchorMarker << ToByteVector(anchor.GetHash());

    /// @todo @maxb temporary, tests
    auto rawtx = spv::CreateAnchorTx("8c7a0268364d5a5a0696ba50e51e2b23c2f288acdca1e0188324fe3caee720b6", 2, 2663303, "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP", ToByteVector(scriptMeta));

    bool send = true;
    if (send)
        spv::pspv->SendRawTx(rawtx);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << anchor;

    CMutableTransaction mtx;
    /// @todo @maxb implement separated bitcoin serialize/deserialize
    DecodeHexTx(mtx, std::string(rawtx.begin(), rawtx.end()), true);

    UniValue result(UniValue::VOBJ);
    result.pushKV("anchorMsg", HexStr(ss.begin(), ss.end()));
    result.pushKV("anchorMsgHash", anchor.GetHash().ToString());
    result.pushKV("txHex", HexStr(rawtx));
    /// @attention WRONG HASH!!!
    result.pushKV("txHash", CTransaction(mtx).GetHash().ToString());

    return result;
}

UniValue spv_createanchortemplate(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_createanchortemplate",
        "\nCreates (and submits to local node and network) a masternode creation transaction with given metadata, spending the given inputs..\n"
        "The first optional argument (may be empty array) is an array of specific UTXOs to spend." +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
            {"rewardAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "User's P2PKH address for reward"},
            {"hash", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "ID of block in DF chain to anchor to. Current ChaiTip if omitted." },
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

//    RPCTypeCheck(request.params[0], { UniValue::VOBJ }, true);
    if (request.params[0].isNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, argument 1 expected as object with "
                                                  "{\"hash\",\"rewardAddress\"}");
    }

    std::string rewardAddress = request.params[0].getValStr();
    uint256 const hash = request.params[1].isNull() ? uint256() : ParseHashV(request.params[1], "hash");


    auto locked_chain = pwallet->chain().lock();
    CAnchorMessage const anchor = createAnchorMessage(rewardAddress, hash);
    CScript scriptMeta = CScript() << OP_RETURN << spv::BtcAnchorMarker << ToByteVector(anchor.GetHash());

    CMutableTransaction rawTx;
    rawTx.vout.push_back(CTxOut(0, scriptMeta));

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
    spv::TBytes const rawscript(spv::CreateScriptForAddress(Params().GetConsensus().spv.anchors_address));
    // This should not happen or spv.anchors_address is WRONG!
    assert (rawscript.size() != 0);
    rawTx.vout.push_back(CTxOut(Params().GetConsensus().spv.creationFee, CScript(rawscript.begin(), rawscript.end())));

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << anchor;

    UniValue result(UniValue::VOBJ);
    result.pushKV("anchorMsg", HexStr(ss.begin(), ss.end()));
    result.pushKV("anchorMsgHash", anchor.GetHash().ToString());
    result.pushKV("txHex", EncodeHexTx(CTransaction(rawTx)));
    result.pushKV("txHash", CTransaction(rawTx).GetHash().ToString());
    return result;
}

UniValue spv_rescan(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_rescan",
        "\nRescan from block height...\n",
        {
            {"height", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Block height or (tip-height) if negative."},
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

    int height = request.params[0].isNull() ? 0 : request.params[0].get_int();

    if (!spv::pspv->Rescan(height))
        throw JSONRPCError(RPC_MISC_ERROR, "SPV not connected");

    return {};
}

UniValue spv_syncstatus(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_syncstatus",
        "\nRescan from block height...\n",
        {
            {"height", RPCArg::Type::NUM, RPCArg::Optional::OMITTED, "Block height or (tip-height) if negative."},
        },
        RPCResult{
            "{                           (json object)\n"
            "   \"connected\"                (bool) Last synced block\n"
            "   \"current\"                  (num) Last synced block\n"
            "   \"estimated\"                (num) Last synced block\n"
            "}\n"
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


    UniValue result(UniValue::VOBJ);
    result.pushKV("connected", spv::pspv->IsConnected());
    result.pushKV("current", static_cast<int>(spv::pspv->GetLastBlockHeight()));
    result.pushKV("estimated", static_cast<int>(spv::pspv->GetEstimatedBlockHeight()));
    result.pushKV("txCount", static_cast<int>(spv::pspv->GetWalletTxs().size()));
    return result;
}

UniValue spv_gettxconfirmations(const JSONRPCRequest& request)
{
    RPCHelpMan{"spv_gettxconfirmations",
        "\nRescan from block height...\n",
        {
            {"txhash", RPCArg::Type::STR, RPCArg::Optional::NO, "Block height or (tip-height) if negative."},
        },
        RPCResult{
            "{                           (json object)\n"
            "   \"connected\"                (bool) Last synced block\n"
            "   \"current\"                  (num) Last synced block\n"
            "   \"estimated\"                (num) Last synced block\n"
            "}\n"
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

    uint256 txHash;
    ParseHashStr(request.params[0].getValStr(), txHash);
    return UniValue(spv::pspv->GetTxConfirmations(txHash));
}

UniValue spv_sendanchormessage(const JSONRPCRequest& request)
{
    // only for lock
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_sendanchormessage",
        "\nSending anchor message (if TX with anchored hash of this message exists and confirmed on BTC chain)...\n",
        {
            {"msghex", RPCArg::Type::STR, RPCArg::Optional::NO, "Serialized message in hex."},
        },
        RPCResult{
            "msghex                      (str) Message hash\n"
            "}\n"
        },
        RPCExamples{
            HelpExampleCli("spv_sendanchormessage", "txhex")
            + HelpExampleRpc("mn_create", "\"[{\\\"txid\\\":\\\"id\\\",\\\"vout\\\":0}]\" "
                                          "\"{\\\"operatorAuthAddress\\\":\\\"address\\\","
                                             "\\\"collateralAddress\\\":\\\"address\\\""
                                            "}\"")
        },
    }.Check(request);

    std::vector<unsigned char> anchor_data{ParseHex(request.params[0].getValStr())};
    CAnchorMessage anchor;
    CDataStream ser_anchor(anchor_data, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ser_anchor >> anchor;
    } catch (const std::exception&) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Can't deserialize message");
    }

    uint256 const msgHash{anchor.GetHash()};
    {
        auto locked_chain = pwallet->chain().lock();
        if (panchors->ExistAnchorByMsg(msgHash)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Anchor message with hash " + msgHash.ToString() + " already exists!");
        }
    }
    using ConfirmationSigs = CAnchorIndex::ConfirmationSigs;
    ProcessNewAnchor(anchor, {}, false, *g_connman, nullptr);

//    try {
//        // check spv anchor existance
//        spv::BtcAnchorTx spv_anc{};
//        {
//            LOCK(spv::pspv->GetCS());

//            int confs{0};
//            if (!anchor.previousAnchor.IsNull()) {
//                confs = spv::pspv->GetTxConfirmations(anchor.previousAnchor);
//                if (confs < 0) {
//                    throw std::runtime_error("Previous anchor tx " + anchor.previousAnchor.ToString() + ") does not exist!");
//                }
//                else if (confs < 6) {
//                    throw std::runtime_error("Previous anchor tx " + anchor.previousAnchor.ToString() + " has not enough confirmations: " + std::to_string(confs));
//                }
//            }
//            confs = spv::pspv->GetTxConfirmationsByMsg(msgHash);
//            if (confs < 0) {
//                throw std::runtime_error("Anchor tx with message hash " + msgHash.ToString() + ") does not exist!");
//            }
//            else if (confs < 6) {
//                throw std::runtime_error("Anchor tx with message hash " + msgHash.ToString() + " has not enough confirmations: " + std::to_string(confs));
//            }
//            spv_anc = *spv::pspv->GetAnchorTxByMsg(msgHash);
//        }
//        auto locked_chain = pwallet->chain().lock();

//        ValidateAnchor(anchor);
//        if (panchors->AddAnchor(anchor, &spv_anc, {})) {
//            RelayAnchor(msgHash, *g_connman);
//        }

//    } catch (std::runtime_error const & e) {
//        throw JSONRPCError(RPC_MISC_ERROR, e.what());
//    }
    return UniValue(msgHash.ToString());
}

static const CRPCCommand commands[] =
{ //  category          name                        actor (function)            params
  //  ----------------- ------------------------    -----------------------     ----------
  { "spv",      "spv_sendrawtx",              &spv_sendrawtx,             { "rawtx" }  },
  { "spv",      "spv_createanchor",           &spv_createanchor,          { /*"inputs", "hash", "rewardaddress", "privkey" */}  },
  { "spv",      "spv_createanchortemplate",   &spv_createanchortemplate,  { /*"inputs", "hash", "rewardaddress", "privkey" */}  },
  { "spv",      "spv_rescan",                 &spv_rescan,                { "height"}  },
  { "spv",      "spv_syncstatus",             &spv_syncstatus,            { }  },
  { "spv",      "spv_gettxconfirmations",     &spv_gettxconfirmations,    { "txhash" }  },
  { "spv",      "spv_sendanchormessage",      &spv_sendanchormessage,    { "msghex" }  },

//  { "spv",      "mn_resign",                &mn_resign,                 { "inputs", "mn_id" }  },

//  { "spv",      "mn_list",                  &mn_list,                   { "list", "verbose" } },

};

void RegisterSpvRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
