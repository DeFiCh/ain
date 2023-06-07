#include <masternodes/mn_rpc.h>

#include <ain_rs_exports.h>
#include <key_io.h>
#include <util/strencodings.h>
#include <index/txindex.h>
#include "BRCrypto.h"
#include "BRLargeInt.h"
#include "BRInt.h"

enum class EvmMapType {
    AUTO,
    DVM_ADDRESS_TO_EVM,
    EVM_ADDRESS_TO_DVM,
    DVM_TX_TO_EVM,
    EVM_TX_TO_DVM,
    DVM_BLOCK_TO_EVM,
    EVM_BLOCK_TO_DVM
};

static int NumEvmMapType = 7;

UniValue evmtx(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"evmtx",
                "Creates (and submits to local node and network) a tx to send DFI token to EVM address.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "From Eth address"},
                    {"nonce", RPCArg::Type::NUM, RPCArg::Optional::NO, "Transaction nonce"},
                    {"gasPrice", RPCArg::Type::NUM, RPCArg::Optional::NO, "Gas Price in Gwei"},
                    {"gasLimit", RPCArg::Type::NUM, RPCArg::Optional::NO, "Gas limit"},
                    {"to", RPCArg::Type::STR, RPCArg::Optional::NO, "To address. Can be empty"},
                    {"value", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount to send"},
                    {"data", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "Hex encoded data. Can be blank."},
                },
                RPCResult{
                        "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
                },
                RPCExamples{
                        HelpExampleCli("evmtx", R"('"<hex>"')")
                        },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    const auto fromDest = DecodeDestination(request.params[0].get_str());
    if (fromDest.index() != WitV16KeyEthHashType) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "from address not an Ethereum address");
    }

    const auto fromEth = std::get<WitnessV16EthHash>(fromDest);
    const CKeyID keyId{fromEth};

    CKey key;
    if (!pwallet->GetKey(keyId, key)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for from address not found in wallet");
    }

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

    // TODO Get chain ID from Params when defined
    const uint64_t chainID{1};

    const arith_uint256 nonceParam = request.params[1].get_int64();
    const auto nonce = ArithToUint256(nonceParam);

    arith_uint256 gasPriceArith = request.params[2].get_int64(); // Price as GWei
    gasPriceArith *= WEI_IN_GWEI; // Convert to Wei
    const uint256 gasPrice = ArithToUint256(gasPriceArith);

    arith_uint256 gasLimitArith = request.params[3].get_int64();
    const uint256 gasLimit = ArithToUint256(gasLimitArith);

    const auto toStr = request.params[4].get_str();
    std::array<uint8_t, 20> to{};
    if (!toStr.empty()) {
        const auto toDest = DecodeDestination(toStr);
        if (toDest.index() != WitV16KeyEthHashType) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "to address not an Ethereum address");
        }

        const auto toEth = std::get<WitnessV16EthHash>(toDest);
        std::copy(toEth.begin(), toEth.end(), to.begin());
    }

    const arith_uint256 valueParam = AmountFromValue(request.params[5]);
    const auto value = ArithToUint256(valueParam * CAMOUNT_TO_WEI * WEI_IN_GWEI);

    rust::Vec<uint8_t> input{};
    if (!request.params[6].isNull()) {
        const auto inputStr = request.params[6].get_str();
        if (!IsHex(inputStr)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Input param expected to be in hex format");
        }

        const auto inputVec = ParseHex(inputStr);
        input.reserve(inputVec.size());
        std::copy(inputVec.begin(), inputVec.end(), input.begin());
    }

    std::array<uint8_t, 32> privKey{};
    std::copy(key.begin(), key.end(), privKey.begin());

    const auto signedTx = create_and_sign_tx(CreateTransactionContext{chainID, nonce.ToArrayReversed(), gasPrice.ToArrayReversed(), gasLimit.ToArrayReversed(), to, value.ToArrayReversed(), input, privKey});

    std::vector<uint8_t> evmTx(signedTx.size());
    std::copy(signedTx.begin(), signedTx.end(), evmTx.begin());

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::EvmTx)
                << CEvmTxMessage{evmTx};

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vin.resize(2);
    rawTx.vin[0].scriptSig = CScript() << OP_0;
    rawTx.vin[1].scriptSig = CScript() << OP_0;

    rawTx.vout.emplace_back(0, scriptMeta);

    // check execution
    CTransactionRef optAuthTx;
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return send(MakeTransactionRef(std::move(rawTx)), optAuthTx)->GetHash().ToString();
}

UniValue evmrawtx(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{"evmrawtx",
                "Creates (and submits to local node and network) a tx to send DFI token to EVM address.\n" +
                HelpRequiringPassphrase(pwallet) + "\n",
                {
                    {"rawtx", RPCArg::Type::STR, RPCArg::Optional::NO, "EVM raw tx"},
                },
                RPCResult{
                        "\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"
                },
                RPCExamples{
                        HelpExampleCli("evmrawtx", R"('"<hex>"')")
                        },
    }.Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Cannot create transactions while still in Initial Block Download");
    }
    pwallet->BlockUntilSyncedToCurrentChain();

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

    const auto signedTx = request.params[0].get_str();

    std::vector<uint8_t> evmTx = ParseHex(signedTx);

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::EvmTx)
                << CEvmTxMessage{evmTx};

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vin.resize(2);
    rawTx.vin[0].scriptSig = CScript() << OP_0;
    rawTx.vin[1].scriptSig = CScript() << OP_0;

    rawTx.vout.emplace_back(0, scriptMeta);

    // check execution
    CTransactionRef optAuthTx;
    execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);

    return send(MakeTransactionRef(std::move(rawTx)), optAuthTx)->GetHash().ToString();
}

UniValue evmmap(const JSONRPCRequest& request) {
    auto pwallet = GetWallet(request);
    RPCHelpMan{"evmmap",
               "Give the equivalent of an address, blockhash or transaction from EVM to DVM\n",
               {
                       {"hash", RPCArg::Type::STR, RPCArg::Optional::NO, "DVM address, EVM blockhash, EVM transaction"},
                       {"type", RPCArg::Type::NUM, RPCArg::Optional::NO, "Type of mapping: 1 - DFI Address to EVM, 2 - EVM to DFI Address, 3 - DFI Tx to EVM, 4 - EVM Tx to DFI, 5 - DFI Block to EVM, 6 - EVM Block to DFI"}
               },
               RPCResult{
                       "\"hash\"                  (string) The hex-encoded string for address, block or transaction\n"
               },
               RPCExamples{
                       HelpExampleCli("evmmap", R"('"<hex>"' 1)")
               },
    }.Check(request);

    const std::string hash = request.params[0].get_str();

    if (request.params[1].get_int() < 7) {
        const auto type = static_cast<EvmMapType>(request.params[1].get_int());
        switch (type) {
            case EvmMapType::DVM_ADDRESS_TO_EVM: {
                const CPubKey key = AddrToPubKey(pwallet, hash);
                return EncodeDestination(WitnessV16EthHash(key.GetID()));
            }
            case EvmMapType::EVM_ADDRESS_TO_DVM: {
                const CPubKey key = AddrToPubKey(pwallet, hash);
                return EncodeDestination(PKHash(key.GetID()));
            }
            case EvmMapType::DVM_TX_TO_EVM: {
                const auto res = pcustomcsview->GetTxHash(CEvmDvmMapType::DvmEvm, uint256S(hash));
                if (!res) {
                    throw JSONRPCError(RPC_INVALID_REQUEST, res.msg);
                }
                else {
                    return res.val->ToString();
                }
            }
            case EvmMapType::EVM_TX_TO_DVM: {
                const auto res = pcustomcsview->GetTxHash(CEvmDvmMapType::EvmDvm, uint256S(hash));
                if (!res) {
                    throw JSONRPCError(RPC_INVALID_REQUEST, res.msg);
                }
                else {
                    return res.val->ToString();
                }
            }
            case EvmMapType::DVM_BLOCK_TO_EVM: {
                const auto res = pcustomcsview->GetBlockHash(CEvmDvmMapType::DvmEvm, uint256S(hash));
                if (!res) {
                    throw JSONRPCError(RPC_INVALID_REQUEST, res.msg);
                }
                else {
                    return res.val->ToString();
                }
            }
            case EvmMapType::EVM_BLOCK_TO_DVM: {
                const auto res = pcustomcsview->GetBlockHash(CEvmDvmMapType::EvmDvm, uint256S(hash));
                if (!res) {
                    throw JSONRPCError(RPC_INVALID_REQUEST, res.msg);
                }
                else
                {
                    return res.val->ToString();
                }
            }
            case EvmMapType::AUTO: {
                return "";
            }
            default:
                break;
        }
    }
    else {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameters, argument \"type\" must be less than %d.", NumEvmMapType));
    }
}

static const CRPCCommand commands[] =
{
//  category        name                         actor (function)        params
//  --------------- ----------------------       ---------------------   ----------
    {"evm",         "evmtx",                     &evmtx,                 {"from", "nonce", "gasPrice", "gasLimit", "to", "value", "data"}},
    {"evm",         "evmrawtx",                  &evmrawtx,              {"rawtx"}},
    {"evm",         "evmmap",                    &evmmap,                { "hash", "type"}},
};

void RegisterEVMRPCCommands(CRPCTable& tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
