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

//extern UniValue createrawtransaction(UniValue const & params, bool fHelp); // in rawtransaction.cpp
//extern UniValue fundrawtransaction(UniValue const & params, bool fHelp); // in rpcwallet.cpp
//extern UniValue signrawtransaction(UniValue const & params, bool fHelp); // in rawtransaction.cpp
//extern UniValue sendrawtransaction(UniValue const & params, bool fHelp); // in rawtransaction.cpp
//extern UniValue getnewaddress(UniValue const & params, bool fHelp); // in rpcwallet.cpp
//extern bool EnsureWalletIsAvailable(bool avoidException); // in rpcwallet.cpp
//extern bool DecodeHexTx(CTransaction & tx, std::string const & strHexTx); // in core_io.h

//extern void ScriptPubKeyToJSON(CScript const & scriptPubKey, UniValue & out, bool fIncludeHex); // in rawtransaction.cpp

//extern void FundTransaction(CWallet* const pwallet, CMutableTransaction& tx, CAmount& fee_out, int& change_position, UniValue options);

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

    if (anchor.sigs.size() < panchorauths->GetMinAnchorQuorum(pmasternodesview->GetCurrentTeam())) {
        throw JSONRPCError(RPC_VERIFY_ERROR, "Min anchor quorum was not reached (" + std::to_string(anchor.sigs.size()) + ", need "+ std::to_string(panchorauths->GetMinAnchorQuorum(pmasternodesview->GetCurrentTeam())) + ") ");
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
    auto rawtx = spv::CreateAnchorTx("b51644d042d3eaf99863c6113d303ddc2ff90aad0f0e0d2cada9c669a7f6dc95", 0, 2863303, "cStbpreCo2P4nbehPXZAAM3gXXY1sAphRfEhj7ADaLx8i2BmxvEP", ToByteVector(scriptMeta));

    bool send = false;
    if (send)
        spv::pspv->SendRawTx(rawtx);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << anchor;

    UniValue result(UniValue::VOBJ);
    result.pushKV("anchorMsg", HexStr(ss.begin(), ss.end()));
    result.pushKV("txHex", HexStr(rawtx));
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
    result.pushKV("txHex", EncodeHexTx(CTransaction(rawTx)));
    return result;
}


static const CRPCCommand commands[] =
{ //  category          name                        actor (function)            params
  //  ----------------- ------------------------    -----------------------     ----------
  { "spv",      "spv_sendrawtx",              &spv_sendrawtx,             { "rawtx" }  },
  { "spv",      "spv_createanchor",           &spv_createanchor,          { /*"inputs", "hash", "rewardaddress", "privkey" */}  },
  { "spv",      "spv_createanchortemplate",   &spv_createanchortemplate,  { /*"inputs", "hash", "rewardaddress", "privkey" */}  },
//  { "spv",      "mn_resign",                &mn_resign,                 { "inputs", "mn_id" }  },

//  { "spv",      "mn_list",                  &mn_list,                   { "list", "verbose" } },

};

void RegisterSpvRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
