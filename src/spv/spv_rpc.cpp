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

spv::TBytes AnchorMarker = { 'D', 'F', 'A'};


static CWallet* GetWallet(const JSONRPCRequest& request)
{
    std::shared_ptr<CWallet> const wallet = GetWalletForJSONRPCRequest(request);
    CWallet* const pwallet = wallet.get();

    EnsureWalletIsAvailable(pwallet, false);
    EnsureWalletIsUnlocked(pwallet);
    return pwallet;
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
            {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "A json array of json objects",
                {
                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                        {
                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
                            {"amount", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount of output"},
                            {"privkey", RPCArg::Type::STR, RPCArg::Optional::NO, "WIF private key for signing this output"},
                        },
                    },
                },
            },
            {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
                {
                    {"hash", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "ID of block in DF chain to anchor to. Current ChaiTip if omitted." },
                    {"rewardAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "User's P2PKH address for reward"},
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

    RPCTypeCheck(request.params, { UniValue::VARR, UniValue::VOBJ }, true);
    if (request.params[0].isNull() || request.params[1].isNull())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameters, arguments 1 and 2 must be non-null, and argument 2 expected as object with "
                                                  "{\"hash\",\"rewardAddress\"}");
    }
//    UniValue metaObj = request.params[1].get_obj();
//    RPCTypeCheckObj(metaObj, {
//                        { "hash", UniValue::VSTR },
//                        { "rewardAddress", UniValue::VSTR }
//                    },
//                    true, true);

//    std::string hash          = metaObj["hash"].getValStr();
//    std::string rewardAddress = metaObj["operatorAuthAddress"].getValStr();

//    CTxDestination collateralDest = DecodeDestination(collateralAddress);
//    if (collateralDest.which() != 1 && collateralDest.which() != 4)
//    {
//        throw JSONRPCError(RPC_INVALID_PARAMETER, "collateralAddress (" + collateralAddress + ") does not refer to a P2PKH or P2WPKH address");
//    }
//    CKeyID ownerAuthKey = collateralDest.which() == 1 ? CKeyID(*boost::get<PKHash>(&collateralDest)) : CKeyID(*boost::get<WitnessV0KeyHash>(&collateralDest));

//    CTxDestination operatorDest = operatorAuthAddressBase58 == "" ? collateralDest : DecodeDestination(operatorAuthAddressBase58);
//    if (operatorDest.which() != 1 && operatorDest.which() != 4)
//    {
//        throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorAuthAddress (" + operatorAuthAddressBase58 + ") does not refer to a P2PKH or P2WPKH address");
//    }
//    CKeyID operatorAuthKey = operatorDest.which() == 1 ? CKeyID(*boost::get<PKHash>(&operatorDest)) : CKeyID(*boost::get<WitnessV0KeyHash>(&operatorDest)) ;

//    {
//        auto locked_chain = pwallet->chain().lock();

//        if (pmasternodesview->ExistMasternode(CMasternodesView::AuthIndex::ByOwner, ownerAuthKey) ||
//            pmasternodesview->ExistMasternode(CMasternodesView::AuthIndex::ByOperator, ownerAuthKey))
//        {
//            throw JSONRPCError(RPC_INVALID_PARAMETER, "Masternode with collateralAddress == " + collateralAddress + " already exists");
//        }
//        if (pmasternodesview->ExistMasternode(CMasternodesView::AuthIndex::ByOwner, operatorAuthKey) ||
//            pmasternodesview->ExistMasternode(CMasternodesView::AuthIndex::ByOperator, operatorAuthKey))
//        {
//            throw JSONRPCError(RPC_INVALID_PARAMETER, "Masternode with operatorAuthAddress == " + EncodeDestination(operatorDest) + " already exists");
//        }
//    }

    CDataStream metadata(AnchorMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata // << static_cast<unsigned char>('A')
             << uint256S("0");

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    auto rawtx = spv::CreateAnchorTx("38ef3035ccdc65573d96bf7496a5f3f3715981cb864a14e384711a6979bc1ea5", 0, 2963303, "cSGoW1M3CMqDPcWjV2bC6n3razzoYNyJPiWXwH5bYULYXkNy2c2r", ToByteVector(scriptMeta));

    bool send = false;
    if (send)
        spv::pspv->SendRawTx(rawtx);

//    CMutableTransaction rawTx;

//    FillInputs(request.params[0].get_array(), rawTx);

//    rawTx.vout.push_back(CTxOut(EstimateMnCreationFee(), scriptMeta));
//    rawTx.vout.push_back(CTxOut(GetMnCollateralAmount(), GetScriptForDestination(collateralDest)));

//    return fundsignsend(rawTx, request, pwallet);
    return HexStr(rawtx);
}

UniValue spv_createanchortemplate(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWallet(request);

    RPCHelpMan{"spv_createanchortemplate",
        "\nCreates (and submits to local node and network) a masternode creation transaction with given metadata, spending the given inputs..\n"
        "The first optional argument (may be empty array) is an array of specific UTXOs to spend." +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
//            {"inputs", RPCArg::Type::ARR, RPCArg::Optional::OMITTED_NAMED_ARG, "A json array of json objects",
//                {
//                    {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
//                        {
//                            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
//                            {"vout", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number"},
//                        },
//                    },
//                },
//            },
//            {"metadata", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "",
//                {
//                    {"hash", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Masternode operator auth address (P2PKH only, unique)" },
//                    {"rewardAddress", RPCArg::Type::STR, RPCArg::Optional::NO, "Any valid address for keeping collateral amount (any P2PKH or P2WKH address) - used as owner key"},
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

    CMutableTransaction rawTx;


    CDataStream metadata(AnchorMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata // << static_cast<unsigned char>('A')
             << uint256S("0");
    CScript const scriptMeta = CScript() << OP_RETURN << ToByteVector(metadata);

    rawTx.vout.push_back(CTxOut(0, scriptMeta));

    // "manually" decode anchor address and construct script;
    uint160 anchorPKHash;
    {
        std::vector<unsigned char> data;
        if (DecodeBase58Check(Params().GetConsensus().spv.anchors_address, data)) {
            // base58-encoded Bitcoin addresses.
            // Public-key-hash-addresses have version 0 (or 111 testnet).
            // The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is the serialized public key.
            const std::vector<unsigned char> pubkey_prefix = { spv::pspv->GetPKHashPrefix() };
            if (data.size() == anchorPKHash.size() + pubkey_prefix.size() && std::equal(pubkey_prefix.begin(), pubkey_prefix.end(), data.begin())) {
                std::copy(data.begin() + pubkey_prefix.size(), data.end(), anchorPKHash.begin());
            }
        }
    }
    CScript const anchorScript = CScript() << OP_DUP << OP_HASH160 << ToByteVector(anchorPKHash) << OP_EQUALVERIFY << OP_CHECKSIG;
    rawTx.vout.push_back(CTxOut(Params().GetConsensus().spv.creationFee, anchorScript));

    return EncodeHexTx(CTransaction(rawTx));
}




static const CRPCCommand commands[] =
{ //  category          name                        actor (function)            params
  //  ----------------- ------------------------    -----------------------     ----------
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
