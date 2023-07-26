#include <masternodes/mn_rpc.h>

#include <ain_rs_exports.h>
#include <key_io.h>
#include <masternodes/errors.h>
#include <util/strencodings.h>

enum class VMDomainRPCMapType {
    Auto,
    TxHashDVMToEVM,
    TxHashEVMToEVM,
    BlockHashDVMToEVM,
    BlockHashEVMToDVM,
    BlockNumberDVMToEVM,
    BlockNumberEVMToDVM,
};

enum class VMAddressType {
    Auto,
    DVMToEVMAddress,
    EVMToDVMAddress,
};

static int VMDomainRPCMapTypeCount = 7;
static int VMAddressTypeCount = 3;

enum class VMDomainIndexType { BlockHash, TxHash };

UniValue evmtx(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "evmtx",
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
        RPCResult{"\"hash\"                  (string) The hex-encoded hash of broadcasted transaction\n"},
        RPCExamples{HelpExampleCli("evmtx", R"('"<hex>"')")},
    }
        .Check(request);

    if (pwallet->chain().isInitialBlockDownload()) {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Cannot create transactions while still in Initial Block Download");
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
    const auto nonce               = ArithToUint256(nonceParam);

    arith_uint256 gasPriceArith = request.params[2].get_int64();  // Price as GWei
    gasPriceArith *= WEI_IN_GWEI;                                 // Convert to Wei
    const uint256 gasPrice = ArithToUint256(gasPriceArith);

    arith_uint256 gasLimitArith = request.params[3].get_int64();
    const uint256 gasLimit      = ArithToUint256(gasLimitArith);

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
    const auto value               = ArithToUint256(valueParam * CAMOUNT_TO_GWEI * WEI_IN_GWEI);

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

    CrossBoundaryResult result;
    const auto signedTx = evm_try_create_and_sign_tx(result,
                                                     CreateTransactionContext{chainID,
                                                                              nonce.GetByteArray(),
                                                                              gasPrice.GetByteArray(),
                                                                              gasLimit.GetByteArray(),
                                                                              to,
                                                                              value.GetByteArray(),
                                                                              input,
                                                                              privKey});
    if (!result.ok) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Failed to create and sign TX: %s", result.reason.c_str()));
    }

    std::vector<uint8_t> evmTx(signedTx.size());
    std::copy(signedTx.begin(), signedTx.end(), evmTx.begin());

    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::EvmTx) << CEvmTxMessage{evmTx};

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

UniValue handleMapBlockNumberEVMToDVMRequest(const std::string &input) {
    uint64_t height;
    bool success = ParseUInt64(input, &height);
    if (!success || height < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, DeFiErrors::InvalidBlockNumberString(input).msg);
    }
    CrossBoundaryResult result;
    auto evmHash = evm_try_get_block_hash_by_number(result, height);
    if (!result.ok) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, result.reason.c_str());
    }
    auto evmBlockHash = std::vector<uint8_t>(evmHash.begin(), evmHash.end());
    std::reverse(evmBlockHash.begin(), evmBlockHash.end());
    ResVal<uint256> dvm_block = pcustomcsview->GetVMDomainBlockEdge(VMDomainEdge::EVMToDVM, uint256(evmBlockHash));
    if (!dvm_block) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, dvm_block.msg);
    }
    CBlockIndex *pindex = LookupBlockIndex(*dvm_block.val);
    return pindex->GetBlockHeader().deprecatedHeight;
}

UniValue handleMapBlockNumberDVMToEVMRequest(const std::string &input) {
    uint64_t height;
    const int current_tip = ::ChainActive().Height();
    bool success          = ParseUInt64(input, &height);
    if (!success || height < 0 || height > static_cast<uint64_t>(current_tip)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, DeFiErrors::InvalidBlockNumberString(input).msg);
    }
    CBlockIndex *pindex = ::ChainActive()[height];
    auto evmBlockHash =
        pcustomcsview->GetVMDomainBlockEdge(VMDomainEdge::DVMToEVM, uint256S(pindex->GetBlockHash().GetHex()));
    if (!evmBlockHash.val.has_value()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, evmBlockHash.msg);
    }
    CrossBoundaryResult result;
    auto blockNumber = evm_try_get_block_number_by_hash(result, evmBlockHash.val.value().GetByteArray());
    if (!result.ok) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, result.reason.c_str());
    }
    return blockNumber;
}

UniValue vmmap(const JSONRPCRequest &request) {
    RPCHelpMan{
        "vmmap",
        "Give the equivalent of an address, blockhash or transaction from EVM to DVM\n",
        {{"input", RPCArg::Type::STR, RPCArg::Optional::NO, "DVM address, EVM blockhash, EVM transaction"},
          {"type",
          RPCArg::Type::NUM,
          RPCArg::Optional::NO,
          "Map types: \n\
                            1 - Tx Hash: DFI -> EVM \n\
                            2 - Tx Hash: EVM -> DFI \n\
                            3 - Block Hash: DFI -> EVM \n\
                            4 - Block Hash: EVM -> DFI \n\
                            5 - Block Number: DFI -> EVM \n\
                            6 - Block Number: EVM -> DFI"}},
        RPCResult{"\"input\"                  (string) The hex-encoded string for address, block or transaction\n"},
        RPCExamples{HelpExampleCli("vmmap", R"('"<hash>"' 1)")},
    }
        .Check(request);

    auto throwInvalidParam = []() { throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid type parameter")); };

    auto ensureEVMHashPrefixed = [](const std::string &str, const VMDomainRPCMapType type) {
        if (type == VMDomainRPCMapType::TxHashDVMToEVM || type == VMDomainRPCMapType::BlockHashDVMToEVM) {
            return "0x" + str;
        }
        return str;
    };

    const std::string input = request.params[0].get_str();

    const int typeInt = request.params[1].get_int();
    if (typeInt < 0 || typeInt >= VMDomainRPCMapTypeCount) {
        throwInvalidParam();
    }
    const auto type = static_cast<VMDomainRPCMapType>(request.params[1].get_int());
    LOCK(cs_main);

    ResVal res = ResVal<uint256>(uint256{}, Res::Ok());
    switch (type) {
        case VMDomainRPCMapType::TxHashDVMToEVM: {
            res = pcustomcsview->GetVMDomainTxEdge(VMDomainEdge::DVMToEVM, uint256S(input));
            break;
        }
        case VMDomainRPCMapType::TxHashEVMToEVM: {
            res = pcustomcsview->GetVMDomainTxEdge(VMDomainEdge::EVMToDVM, uint256S(input));
            break;
        }
        case VMDomainRPCMapType::BlockHashDVMToEVM: {
            res = pcustomcsview->GetVMDomainBlockEdge(VMDomainEdge::DVMToEVM, uint256S(input));
            break;
        }
        case VMDomainRPCMapType::BlockHashEVMToDVM: {
            res = pcustomcsview->GetVMDomainBlockEdge(VMDomainEdge::EVMToDVM, uint256S(input));
            break;
        }
        case VMDomainRPCMapType::BlockNumberDVMToEVM: {
            return handleMapBlockNumberDVMToEVMRequest(input);
        }
        case VMDomainRPCMapType::BlockNumberEVMToDVM: {
            return handleMapBlockNumberEVMToDVMRequest(input);
        }
        default: {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown map type");
        }
    }

    if (!res) {
        throw JSONRPCError(RPC_INVALID_REQUEST, res.msg);
    } else {
        return ensureEVMHashPrefixed(res.val->ToString(), type);
    }
}

UniValue logvmmaps(const JSONRPCRequest &request) {
    RPCHelpMan{
        "logvmmaps",
        "\nLogs all block or tx indexes for debugging.\n",
        {{"type", RPCArg::Type::NUM, RPCArg::Optional::NO, "Type of log: 0 - Blocks, 1 - Txs"}},
        RPCResult{"{...} (array) Json object with account balances if rpcresult is enabled."
                  "This is for debugging purposes only.\n"},
        RPCExamples{HelpExampleCli("logvmmaps", R"('"<hex>"' 1)")},
    }
        .Check(request);

    LOCK(cs_main);

    size_t count{};
    UniValue result{UniValue::VOBJ};
    UniValue indexesJson{UniValue::VOBJ};
    const auto type = static_cast<VMDomainIndexType>(request.params[0].get_int());
    // TODO: For now, we iterate through the whole list. But this is just a debugging RPC.
    // But there's no need to iterate the whole list, we can start at where we need to and
    // return false, once we hit the limit and stop the iter.
    switch (type) {
        case VMDomainIndexType::BlockHash: {
            pcustomcsview->ForEachVMDomainBlockEdges(
                [&](const std::pair<VMDomainEdge, uint256> &index, uint256 blockHash) {
                    if (index.first == VMDomainEdge::DVMToEVM) {
                        indexesJson.pushKV(index.second.GetHex(), blockHash.GetHex());
                        ++count;
                    }
                    return true;
                });
        }
        case VMDomainIndexType::TxHash: {
            pcustomcsview->ForEachVMDomainTxEdges([&](const std::pair<VMDomainEdge, uint256> &index, uint256 txHash) {
                if (index.first == VMDomainEdge::DVMToEVM) {
                    indexesJson.pushKV(index.second.GetHex(), txHash.GetHex());
                    ++count;
                }
                return true;
            });
        }
    }

    result.pushKV("indexes", indexesJson);
    result.pushKV("count", static_cast<uint64_t>(count));
    return result;
}


UniValue vmaddressmap(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);
    RPCHelpMan{
        "vmaddressmap",
        "Give the equivalent of an address from EVM to DVM and versa\n",
        {
            {"input", RPCArg::Type::STR, RPCArg::Optional::NO, "DVM address or EVM address"},
            {"type", RPCArg::Type::NUM, RPCArg::Optional::NO, "Map types: \n\
                            1 - Address format: DFI -> ETH \n\
                            2 - Address format: ETH -> DFI \n"}
        },
        RPCResult{"\"input\"                  (string) The hex-encoded string for address, block or transaction\n"},
        RPCExamples{HelpExampleCli("vmaddressmap", R"('"<address>"' 1)")},
    }
        .Check(request);

    auto throwInvalidParam = []() { throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid type parameter")); };

    const std::string input = request.params[0].get_str();

    const int typeInt = request.params[1].get_int();
    if (typeInt < 0 || typeInt >= VMAddressTypeCount) {
        throwInvalidParam();
    }
    const auto type = static_cast<VMAddressType>(request.params[1].get_int());
    switch (type) {
        case VMAddressType::DVMToEVMAddress: {
            CTxDestination dest = DecodeDestination(input);
            if (dest.index() != WitV0KeyHashType && dest.index() != PKHashType) {
                throwInvalidParam();
            }
            CPubKey key = AddrToPubKey(pwallet, input);
            if (key.IsCompressed()) {
                key.Decompress();
            }
            return EncodeDestination(WitnessV16EthHash(key));
        }
        case VMAddressType::EVMToDVMAddress: {
            CTxDestination dest = DecodeDestination(input);
            if (dest.index() != WitV16KeyEthHashType) {
                throwInvalidParam();
            }
            CPubKey key = AddrToPubKey(pwallet, input);
            if (!key.IsCompressed()) {
                key.Compress();
            }
            return EncodeDestination(WitnessV0KeyHash(key));
        }
        default:
            throw JSONRPCError(RPC_INVALID_REQUEST, "Invalid address type passed");
            break;
    }
}

static const CRPCCommand commands[] = {
  //  category        name                         actor (function)        params
  //  --------------- ----------------------       ---------------------   ----------
    {"evm", "evmtx",            &evmtx,         {"from", "nonce", "gasPrice", "gasLimit", "to", "value", "data"}},
    {"evm", "vmmap",            &vmmap,         {"input", "type"}                                               },
    {"evm", "logvmmaps",        &logvmmaps,     {"type"}                                                        },
    {"evm", "vmaddressmap",     &vmaddressmap,  {"input", "type"}                                               },
};

void RegisterEVMRPCCommands(CRPCTable &tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
