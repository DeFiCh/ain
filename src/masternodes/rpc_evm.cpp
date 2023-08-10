#include <masternodes/mn_rpc.h>

#include <ain_rs_exports.h>
#include <key_io.h>
#include <masternodes/errors.h>
#include <util/strencodings.h>

enum class VMDomainRPCMapType {
    Unknown = -1,
    Auto    = 0,
    BlockNumberDVMToEVM,
    BlockNumberEVMToDVM,
    BlockHashDVMToEVM,
    BlockHashEVMToDVM,
    TxHashDVMToEVM,
    TxHashEVMToDVM,
};

std::string GetVMDomainRPCMapType(VMDomainRPCMapType t) {
    switch (t) {
        case VMDomainRPCMapType::Auto:
            return "Auto";
        case VMDomainRPCMapType::BlockNumberDVMToEVM:
            return "BlockNumberDVMToEVM";
        case VMDomainRPCMapType::BlockNumberEVMToDVM:
            return "BlockNumberEVMToDVM";
        case VMDomainRPCMapType::BlockHashDVMToEVM:
            return "BlockHashDVMToEVM";
        case VMDomainRPCMapType::BlockHashEVMToDVM:
            return "BlockHashEVMToDVM";
        case VMDomainRPCMapType::TxHashDVMToEVM:
            return "TxHashDVMToEVM";
        case VMDomainRPCMapType::TxHashEVMToDVM:
            return "TxHashEVMToDVM";
        default:
            return "Unknown";
    }
}

static int VMDomainRPCMapTypeCount = 7;

enum class VMDomainIndexType { BlockHashDVMToEVM, BlockHashEVMToDVM, TxHashDVMToEVM, TxHashEVMToDVM };

UniValue evmtx(const JSONRPCRequest &request) {
    auto pwallet = GetWallet(request);

    RPCHelpMan{
        "evmtx",
        "Creates (and submits to local node and network) a tx to send DFI token to EVM address.\n" +
            HelpRequiringPassphrase(pwallet) + "\n",
        {
                          {"from", RPCArg::Type::STR, RPCArg::Optional::NO, "From ERC55 address"},
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

UniValue vmmap(const JSONRPCRequest &request) {
    RPCHelpMan{
        "vmmap",
        "Give the equivalent of an address, blockhash or transaction from EVM to DVM\n",
        {{"input", RPCArg::Type::STR, RPCArg::Optional::NO, "DVM address, EVM blockhash, EVM transaction"},
          {"type",
          RPCArg::Type::NUM,
          RPCArg::Optional::NO,
          "Map types: \n\
                            0 - Auto \n\
                            1 - Block Number: DFI -> EVM (Unsupported yet) \n\
                            2 - Block Number: EVM -> DFI (Unsupported yet) \n\
                            3 - Block Hash: DFI -> EVM \n\
                            4 - Block Hash: EVM -> DFI \n\
                            5 - Tx Hash: DFI -> EVM \n\
                            6 - Tx Hash: EVM -> DFI \n"}},
        RPCResult{"\"input\"                  (string) The hex-encoded string for address, block or transaction\n\
                                            or (number) block number\n"},
        RPCExamples{HelpExampleCli("vmmap", R"('"<hash>"' 1)")},
    }
        .Check(request);

    auto throwInvalidParam = [](std::string msg = "") {
        throw JSONRPCError(RPC_INVALID_PARAMETER, msg.length() > 0 ? msg : "Invalid parameter");
    };
    auto throwUnsupportedAuto = [&throwInvalidParam]() {
        throwInvalidParam("Automatic detection not viable for input");
    };

    auto ensureEVMHashPrefixed = [](const std::string &str, const VMDomainRPCMapType type) {
        if (type == VMDomainRPCMapType::TxHashDVMToEVM || type == VMDomainRPCMapType::BlockHashDVMToEVM) {
            return "0x" + str;
        }
        return str;
    };

    const std::string input = request.params[0].get_str();

    int typeInt = request.params[1].get_int();
    if (typeInt < 0 || typeInt >= VMDomainRPCMapTypeCount) {
        throwInvalidParam();
    }

    auto tryResolveMapBlockOrTxResult = [](ResVal<uint256> &res, const uint256 input) {
        res = pcustomcsview->GetVMDomainTxEdge(VMDomainEdge::DVMToEVM, input);
        if (res)
            return VMDomainRPCMapType::TxHashDVMToEVM;

        res = pcustomcsview->GetVMDomainTxEdge(VMDomainEdge::EVMToDVM, input);
        if (res)
            return VMDomainRPCMapType::TxHashEVMToDVM;

        res = pcustomcsview->GetVMDomainBlockEdge(VMDomainEdge::DVMToEVM, input);
        if (res)
            return VMDomainRPCMapType::BlockHashDVMToEVM;

        res = pcustomcsview->GetVMDomainBlockEdge(VMDomainEdge::EVMToDVM, input);
        if (res)
            return VMDomainRPCMapType::BlockHashEVMToDVM;

        return VMDomainRPCMapType::Unknown;
    };
/*
    auto crossBoundaryOkOrThrow = [&throwInvalidParam](CrossBoundaryResult &result) {
        if (!result.ok) {
            throwInvalidParam(result.reason.c_str());
        }
    };

    auto tryResolveBlockNumberType =
        [&throwUnsupportedAuto, &crossBoundaryOkOrThrow](const std::string input) {
            uint64_t height;
            if (!ParseUInt64(input, &height)) {
                return VMDomainRPCMapType::Unknown;
            }
            CrossBoundaryResult result;
            auto evmBlockCount = evm_try_get_block_count(result);
            crossBoundaryOkOrThrow(result);

            // evm block count always less than dvm block count
            if (height > evmBlockCount) {
                return VMDomainRPCMapType::BlockNumberDVMToEVM;
            } else {
                // auto evmFirstBlock = evm_try_get_first_block(result);
                // crossBoundaryOkOrThrow(result);
                // if (height >= evmFirstBlock && height <= evmBlockCount) {
                //     return VMDomainRPCMapType::BlockNumberEVMToDVM;
                // }
                throwUnsupportedAuto();
                return VMDomainRPCMapType::Unknown;
            }
        };
*/
    auto type  = static_cast<VMDomainRPCMapType>(typeInt);
    ResVal res = ResVal<uint256>(uint256{}, Res::Ok());

    auto handleAutoInfer = [&]() -> std::tuple<VMDomainRPCMapType, bool> {
        // auto mapType = tryResolveBlockNumberType(input);
        // if (mapType != VMDomainRPCMapType::Unknown)
        //     return {mapType, false};

        auto inLength = input.length();
        if (inLength == 64 || inLength == 66) {
            auto mapType = tryResolveMapBlockOrTxResult(res, uint256S(input));
            // We don't pass this type back on purpose
            if (mapType != VMDomainRPCMapType::Unknown) {
                return {mapType, true};
            }
        }
        throwUnsupportedAuto();
        return {VMDomainRPCMapType::Unknown, false};
    };

    auto finalizeResult = [&](ResVal<uint256> &res, const VMDomainRPCMapType type, const std::string input) {
        if (!res) {
            throw JSONRPCError(RPC_INVALID_REQUEST, res.msg);
        } else {
            UniValue ret(UniValue::VOBJ);
            ret.pushKV("input", input);
            ret.pushKV("type", GetVMDomainRPCMapType(type));
            ret.pushKV("output", ensureEVMHashPrefixed(res.val->ToString(), type));
            return ret;
        }
    };
/*
    auto finalizeBlockNumberResult = [&](uint64_t &number, const VMDomainRPCMapType type, const uint64_t input) {
        UniValue ret(UniValue::VOBJ);
        ret.pushKV("input", input);
        ret.pushKV("type", GetVMDomainRPCMapType(type));
        ret.pushKV("output", number);
        return ret;
    };

    auto handleMapBlockNumberEVMToDVMRequest = [&throwInvalidParam,
                                                &finalizeBlockNumberResult,
                                                &crossBoundaryOkOrThrow](const std::string &input) -> UniValue {
        uint64_t height;
        bool success = ParseUInt64(input, &height);
        if (!success || height < 0) {
            throwInvalidParam(DeFiErrors::InvalidBlockNumberString(input).msg.c_str());
        }
        CrossBoundaryResult result;
        auto evmHash = evm_try_get_block_hash_by_number(result, height);
        crossBoundaryOkOrThrow(result);
        auto evmBlockHash = std::vector<uint8_t>(evmHash.begin(), evmHash.end());
        std::reverse(evmBlockHash.begin(), evmBlockHash.end());
        ResVal<uint256> dvm_block = pcustomcsview->GetVMDomainBlockEdge(VMDomainEdge::EVMToDVM, uint256(evmBlockHash));
        if (!dvm_block) {
            throwInvalidParam(dvm_block.msg);
        }
        CBlockIndex *pindex  = LookupBlockIndex(*dvm_block.val);
        uint64_t blockNumber = pindex->GetBlockHeader().deprecatedHeight;
        return finalizeBlockNumberResult(blockNumber, VMDomainRPCMapType::BlockNumberEVMToDVM, height);
    };

    auto handleMapBlockNumberDVMToEVMRequest = [&throwInvalidParam,
                                                &finalizeBlockNumberResult,
                                                &crossBoundaryOkOrThrow](const std::string &input) -> UniValue {
        uint64_t height;
        const int current_tip = ::ChainActive().Height();
        bool success          = ParseUInt64(input, &height);
        if (!success || height < 0 || height > static_cast<uint64_t>(current_tip)) {
            throwInvalidParam(DeFiErrors::InvalidBlockNumberString(input).msg);
        }
        CBlockIndex *pindex = ::ChainActive()[height];
        auto evmBlockHash =
            pcustomcsview->GetVMDomainBlockEdge(VMDomainEdge::DVMToEVM, uint256S(pindex->GetBlockHash().GetHex()));
        if (!evmBlockHash.val.has_value()) {
            throwInvalidParam(evmBlockHash.msg);
        }
        CrossBoundaryResult result;
        uint64_t blockNumber = evm_try_get_block_number_by_hash(result, evmBlockHash.val.value().GetByteArray());
        crossBoundaryOkOrThrow(result);
        return finalizeBlockNumberResult(blockNumber, VMDomainRPCMapType::BlockNumberDVMToEVM, height);
    };
*/
    LOCK(cs_main);

    if (type == VMDomainRPCMapType::Auto) {
        auto [mapType, isResolved] = handleAutoInfer();
        if (isResolved) {
            return finalizeResult(res, mapType, input);
        }
        type = mapType;
    }

    switch (type) {
        case VMDomainRPCMapType::TxHashDVMToEVM: {
            res = pcustomcsview->GetVMDomainTxEdge(VMDomainEdge::DVMToEVM, uint256S(input));
            break;
        }
        case VMDomainRPCMapType::TxHashEVMToDVM: {
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
        // TODO(canonbrother): disable for release, more investigation needed
        // case VMDomainRPCMapType::BlockNumberDVMToEVM: {
        //     return handleMapBlockNumberDVMToEVMRequest(input);
        // }
        // case VMDomainRPCMapType::BlockNumberEVMToDVM: {
        //     return handleMapBlockNumberEVMToDVMRequest(input);
        // }
        default: {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown map type");
        }
    }

    return finalizeResult(res, type, input);
}

UniValue logvmmaps(const JSONRPCRequest &request) {
    RPCHelpMan{
        "logvmmaps",
        "\nLogs all block or tx indexes for debugging.\n",
        {{"type",
          RPCArg::Type::NUM,
          RPCArg::Optional::NO,
          "Type of log:\n"
          "    0 - DVMToEVM Blocks\n"
          "    1 - EVMToDVM Blocks\n"
          "    2 - DVMToEVM TXs\n"
          "    3 - EVMToDVM TXs"}},
        RPCResult{"{...} (array) Json object with account balances if rpcresult is enabled."
                  "This is for debugging purposes only.\n"},
        RPCExamples{HelpExampleCli("logvmmaps", R"('"<hex>"' 1)")},
    }
        .Check(request);

    LOCK(cs_main);

    uint64_t count{};
    UniValue result{UniValue::VOBJ};
    UniValue indexesJson{UniValue::VOBJ};
    const auto type = static_cast<VMDomainIndexType>(request.params[0].get_int());
    // TODO: For now, we iterate through the whole list. But this is just a debugging RPC.
    // But there's no need to iterate the whole list, we can start at where we need to and
    // return false, once we hit the limit and stop the iter.
    switch (type) {
        case VMDomainIndexType::BlockHashDVMToEVM: {
            pcustomcsview->ForEachVMDomainBlockEdges(
                [&](const std::pair<VMDomainEdge, uint256> &index, const uint256 &blockHash) {
                    if (index.first == VMDomainEdge::DVMToEVM) {
                        indexesJson.pushKV(index.second.GetHex(), blockHash.GetHex());
                        ++count;
                    }
                    return true;
                },
                std::make_pair(VMDomainEdge::DVMToEVM, uint256{}));
            break;
        }
        case VMDomainIndexType::BlockHashEVMToDVM: {
            pcustomcsview->ForEachVMDomainBlockEdges(
                [&](const std::pair<VMDomainEdge, uint256> &index, const uint256 &blockHash) {
                    if (index.first == VMDomainEdge::EVMToDVM) {
                        indexesJson.pushKV(index.second.GetHex(), blockHash.GetHex());
                        ++count;
                    }
                    return true;
                },
                std::make_pair(VMDomainEdge::EVMToDVM, uint256{}));
            break;
        }
        case VMDomainIndexType::TxHashDVMToEVM: {
            pcustomcsview->ForEachVMDomainTxEdges(
                [&](const std::pair<VMDomainEdge, uint256> &index, const uint256 &txHash) {
                    if (index.first == VMDomainEdge::DVMToEVM) {
                        indexesJson.pushKV(index.second.GetHex(), txHash.GetHex());
                        ++count;
                    }
                    return true;
                },
                std::make_pair(VMDomainEdge::DVMToEVM, uint256{}));
            break;
        }
        case VMDomainIndexType::TxHashEVMToDVM: {
            pcustomcsview->ForEachVMDomainTxEdges(
                [&](const std::pair<VMDomainEdge, uint256> &index, const uint256 &txHash) {
                    if (index.first == VMDomainEdge::EVMToDVM) {
                        indexesJson.pushKV(index.second.GetHex(), txHash.GetHex());
                        ++count;
                    }
                    return true;
                },
                std::make_pair(VMDomainEdge::EVMToDVM, uint256{}));
            break;
        }
        default:
            throw JSONRPCError(RPC_INVALID_PARAMETER, "type out of range");
    }

    result.pushKV("indexes", indexesJson);
    result.pushKV("count", count);
    return result;
}

static const CRPCCommand commands[] = {
  //  category        name                         actor (function)        params
  //  --------------- ----------------------       ---------------------   ----------
    {"evm", "evmtx",     &evmtx,     {"from", "nonce", "gasPrice", "gasLimit", "to", "value", "data"}},
    {"evm", "vmmap",     &vmmap,     {"input", "type"}                                               },
    {"evm", "logvmmaps", &logvmmaps, {"type"}                                                        },
};

void RegisterEVMRPCCommands(CRPCTable &tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
