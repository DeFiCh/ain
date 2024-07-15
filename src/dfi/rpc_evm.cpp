#include <dfi/mn_rpc.h>

#include <ain_rs_exports.h>
#include <dfi/accountshistory.h>
#include <dfi/errors.h>
#include <dfi/vaulthistory.h>
#include <ffi/ffihelpers.h>
#include <key_io.h>
#include <util/strencodings.h>

enum class VMDomainRPCMapType {
    Unknown = -1,
    Auto = 0,
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
                          {"value", RPCArg::Type::NUM, RPCArg::Optional::NO, "Amount to send in DFI"},
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

    EnsureWalletIsUnlocked(pwallet);

    const auto fromDest = DecodeDestination(request.params[0].get_str());
    if (fromDest.index() != WitV16KeyEthHashType) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "from address not an Ethereum address");
    }

    const auto fromEth = std::get<WitnessV16EthHash>(fromDest);
    const CKeyID keyId{fromEth};
    const auto from = fromEth.GetByteArray();

    CKey key;
    if (!pwallet->GetKey(keyId, key)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for from address not found in wallet");
    }

    auto [view, accountView, vaultView] = GetSnapshots();
    auto targetHeight = view->GetLastHeight() + 1;

    // TODO Get chain ID from Params when defined
    const uint64_t chainID{1};

    if (request.params[1].get_int64() < 0 || request.params[2].get_int64() < 0 || request.params[3].get_int64() < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Input params cannot be negative");
    }
    const auto nonce = static_cast<uint64_t>(request.params[1].get_int64());
    const auto gasPrice = static_cast<uint64_t>(request.params[2].get_int64());  // Price as GWei
    const auto gasLimit = static_cast<uint64_t>(request.params[3].get_int64());
    const uint64_t value = AmountFromValue(request.params[5]);  // Amount in CAmount

    const auto toStr = request.params[4].get_str();
    EvmAddressData to{};
    if (!toStr.empty()) {
        const auto toDest = DecodeDestination(toStr);
        if (toDest.index() != WitV16KeyEthHashType) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "to address not an Ethereum address");
        }

        const auto toEth = std::get<WitnessV16EthHash>(toDest);
        to = toEth.GetByteArray();
    }

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
    const auto createResult = evm_try_create_and_sign_tx(
        result, CreateTransactionContext{chainID, nonce, gasPrice, gasLimit, to, value, input, privKey});
    if (!result.ok) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Failed to create and sign TX: %s", result.reason.c_str()));
    }

    std::vector<uint8_t> evmTx(createResult.tx.size());
    std::copy(createResult.tx.begin(), createResult.tx.end(), evmTx.begin());

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
    evm_try_store_account_nonce(result, from, createResult.nonce);
    if (!result.ok) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Could not cache nonce");
    }

    return send(MakeTransactionRef(std::move(rawTx)), optAuthTx)->GetHash().ToString();
}

UniValue vmmap(const JSONRPCRequest &request) {
    RPCHelpMan{
        "vmmap",
        "Give the equivalent of an address, blockhash or transaction from EVM to DVM\n",
        {{"input", RPCArg::Type::STR, RPCArg::Optional::NO, "DVM address, EVM blockhash, EVM transaction"},
          {"type",
          RPCArg::Type::NUM,
          RPCArg::Optional::OMITTED,
          "Map types: \n\
                            0 - Auto (Default) \n\
                            1 - Block Number: DVM -> EVM \n\
                            2 - Block Number: EVM -> DVM \n\
                            3 - Block Hash: DVM -> EVM \n\
                            4 - Block Hash: EVM -> DVM \n\
                            5 - Tx Hash: DVM -> EVM \n\
                            6 - Tx Hash: EVM -> DVM \n"}},
        RPCResult{"\"input\"                  (string) The hex-encoded string for address, block or transaction\n\
                                            or (number) block number\n"},
        RPCExamples{HelpExampleCli("vmmap", R"('"<hash>"' 1)")},
    }
        .Check(request);

    auto throwInvalidParam = [](const std::string &msg = "") {
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

    auto ensureEVMHashStripped = [](const std::string &str) {
        if (str.length() > 2 && str.substr(0, 2) == "0x") {
            return str.substr(2);
        }
        return str;
    };

    const auto inputStr = request.params[0].get_str();
    const auto input = ensureEVMHashStripped(inputStr);

    int typeInt = 0;
    if (!request.params[1].isNull()) {
        typeInt = request.params[1].get_int();
    }
    if (typeInt < 0 || typeInt >= VMDomainRPCMapTypeCount) {
        throwInvalidParam();
    }

    auto [view, accountView, vaultView] = GetSnapshots();

    auto tryResolveMapBlockOrTxResult = [&view = view](ResVal<std::string> &res, const std::string &input) {
        res = view->GetVMDomainTxEdge(VMDomainEdge::DVMToEVM, input);
        if (res) {
            return VMDomainRPCMapType::TxHashDVMToEVM;
        }

        res = view->GetVMDomainTxEdge(VMDomainEdge::EVMToDVM, input);
        if (res) {
            return VMDomainRPCMapType::TxHashEVMToDVM;
        }

        res = view->GetVMDomainBlockEdge(VMDomainEdge::DVMToEVM, input);
        if (res) {
            return VMDomainRPCMapType::BlockHashDVMToEVM;
        }

        res = view->GetVMDomainBlockEdge(VMDomainEdge::EVMToDVM, input);
        if (res) {
            return VMDomainRPCMapType::BlockHashEVMToDVM;
        }

        return VMDomainRPCMapType::Unknown;
    };

    auto crossBoundaryOkOrThrow = [&throwInvalidParam](CrossBoundaryResult &result) {
        if (!result.ok) {
            throwInvalidParam(result.reason.c_str());
        }
    };

    auto tryResolveBlockNumberType = [](const std::string &input) {
        uint64_t height;
        if (!ParseUInt64(input, &height)) {
            return VMDomainRPCMapType::Unknown;
        }

        CrossBoundaryResult evmResult;
        evm_try_get_block_hash_by_number(evmResult, height);
        CBlockIndex *dvmBlock;
        {
            LOCK(cs_main);
            dvmBlock = ::ChainActive()[static_cast<int>(height)];
        }
        if (evmResult.ok && dvmBlock != nullptr) {
            return VMDomainRPCMapType::Unknown;
        } else if (evmResult.ok) {
            return VMDomainRPCMapType::BlockNumberEVMToDVM;
        } else if (dvmBlock != nullptr) {
            return VMDomainRPCMapType::BlockNumberDVMToEVM;
        } else {
            return VMDomainRPCMapType::Unknown;
        }
    };

    auto type = static_cast<VMDomainRPCMapType>(typeInt);
    ResVal res = ResVal<std::string>(std::string{}, Res::Ok());

    auto handleAutoInfer = [&]() -> std::tuple<VMDomainRPCMapType, bool> {
        auto mapType = tryResolveBlockNumberType(input);
        if (mapType != VMDomainRPCMapType::Unknown) {
            return {mapType, false};
        }

        auto inLength = input.length();
        if (inLength == 64 || inLength == 66) {
            mapType = tryResolveMapBlockOrTxResult(res, input);
            // We don't pass this type back on purpose
            if (mapType != VMDomainRPCMapType::Unknown) {
                return {mapType, true};
            }
        }
        throwUnsupportedAuto();
        return {VMDomainRPCMapType::Unknown, false};
    };

    auto finalizeResult = [&](ResVal<std::string> &res, const VMDomainRPCMapType type, const std::string &input) {
        if (!res) {
            throw JSONRPCError(RPC_INVALID_REQUEST, res.msg);
        } else {
            UniValue ret(UniValue::VOBJ);
            ret.pushKV("input", inputStr);
            ret.pushKV("type", GetVMDomainRPCMapType(type));
            ret.pushKV("output", ensureEVMHashPrefixed(*res.val, type));
            return ret;
        }
    };

    auto handleMapBlockNumberDVMToEVMRequest =
        [&view = view, &throwInvalidParam, &crossBoundaryOkOrThrow](const std::string &input) {
            uint64_t height;
            const int current_tip = ::ChainActive().Height();
            bool success = ParseUInt64(input, &height);
            if (!success || height < 0 || height > static_cast<uint64_t>(current_tip)) {
                throwInvalidParam(DeFiErrors::InvalidBlockNumberString(input).msg);
            }
            CBlockIndex *pindex;
            {
                LOCK(cs_main);
                pindex = ::ChainActive()[static_cast<int>(height)];
            }
            auto evmBlockHash = view->GetVMDomainBlockEdge(VMDomainEdge::DVMToEVM, pindex->GetBlockHash().GetHex());
            if (!evmBlockHash.val.has_value()) {
                throwInvalidParam(evmBlockHash.msg);
            }
            auto hash = uint256S(evmBlockHash);
            CrossBoundaryResult result;
            uint64_t blockNumber = evm_try_get_block_number_by_hash(result, hash.GetByteArray());
            crossBoundaryOkOrThrow(result);
            return ResVal<std::string>(std::to_string(blockNumber), Res::Ok());
        };

    auto handleMapBlockNumberEVMToDVMRequest =
        [&view = view, &throwInvalidParam, &crossBoundaryOkOrThrow](const std::string &input) {
            uint64_t height;
            bool success = ParseUInt64(input, &height);
            if (!success || height < 0) {
                throwInvalidParam(DeFiErrors::InvalidBlockNumberString(input).msg);
            }
            CrossBoundaryResult result;
            auto hash = evm_try_get_block_hash_by_number(result, height);
            auto evmBlockHash = uint256::FromByteArray(hash).GetHex();
            crossBoundaryOkOrThrow(result);
            auto dvmBlockHash = view->GetVMDomainBlockEdge(VMDomainEdge::EVMToDVM, evmBlockHash);
            if (!dvmBlockHash.val.has_value()) {
                throwInvalidParam(dvmBlockHash.msg);
            }
            int blockNumber{};
            {
                LOCK(cs_main);
                CBlockIndex *pindex = LookupBlockIndex(uint256S(*dvmBlockHash.val));
                if (!pindex) {
                    throwInvalidParam(DeFiErrors::InvalidBlockHashString(*dvmBlockHash.val).msg);
                }
                blockNumber = pindex->nHeight;
            }
            return ResVal<std::string>(std::to_string(blockNumber), Res::Ok());
        };

    if (type == VMDomainRPCMapType::Auto) {
        auto [mapType, isResolved] = handleAutoInfer();
        if (isResolved) {
            return finalizeResult(res, mapType, input);
        }
        type = mapType;
    }

    switch (type) {
        case VMDomainRPCMapType::TxHashDVMToEVM: {
            res = view->GetVMDomainTxEdge(VMDomainEdge::DVMToEVM, input);
            break;
        }
        case VMDomainRPCMapType::TxHashEVMToDVM: {
            res = view->GetVMDomainTxEdge(VMDomainEdge::EVMToDVM, input);
            break;
        }
        case VMDomainRPCMapType::BlockHashDVMToEVM: {
            res = view->GetVMDomainBlockEdge(VMDomainEdge::DVMToEVM, input);
            break;
        }
        case VMDomainRPCMapType::BlockHashEVMToDVM: {
            res = view->GetVMDomainBlockEdge(VMDomainEdge::EVMToDVM, input);
            break;
        }
        case VMDomainRPCMapType::BlockNumberDVMToEVM: {
            res = handleMapBlockNumberDVMToEVMRequest(input);
            break;
        }
        case VMDomainRPCMapType::BlockNumberEVMToDVM: {
            res = handleMapBlockNumberEVMToDVMRequest(input);
            break;
        }
        default: {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown map type");
        }
    }

    return finalizeResult(res, type, input);
}

UniValue logvmmaps(const JSONRPCRequest &request) {
    RPCHelpMan{
        "logvmmaps",
        "Logs all block or tx indexes for debugging.\n",
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

    auto [view, accountView, vaultView] = GetSnapshots();

    uint64_t count{};
    UniValue result{UniValue::VOBJ};
    UniValue indexesJson{UniValue::VOBJ};
    const auto type = static_cast<VMDomainIndexType>(request.params[0].get_int());
    // TODO: For now, we iterate through the whole list. But this is just a debugging RPC.
    // But there's no need to iterate the whole list, we can start at where we need to and
    // return false, once we hit the limit and stop the iter.
    switch (type) {
        case VMDomainIndexType::BlockHashDVMToEVM: {
            view->ForEachVMDomainBlockEdges(
                [&](const std::pair<VMDomainEdge, std::string> &index, const std::string &blockHash) {
                    if (index.first == VMDomainEdge::DVMToEVM) {
                        indexesJson.pushKV(index.second, blockHash);
                        ++count;
                    }
                    return true;
                },
                std::make_pair(VMDomainEdge::DVMToEVM, std::string{}));
            break;
        }
        case VMDomainIndexType::BlockHashEVMToDVM: {
            view->ForEachVMDomainBlockEdges(
                [&](const std::pair<VMDomainEdge, std::string> &index, const std::string &blockHash) {
                    if (index.first == VMDomainEdge::EVMToDVM) {
                        indexesJson.pushKV(index.second, blockHash);
                        ++count;
                    }
                    return true;
                },
                std::make_pair(VMDomainEdge::EVMToDVM, std::string{}));
            break;
        }
        case VMDomainIndexType::TxHashDVMToEVM: {
            view->ForEachVMDomainTxEdges(
                [&](const std::pair<VMDomainEdge, std::string> &index, const std::string &txHash) {
                    if (index.first == VMDomainEdge::DVMToEVM) {
                        indexesJson.pushKV(index.second, txHash);
                        ++count;
                    }
                    return true;
                },
                std::make_pair(VMDomainEdge::DVMToEVM, std::string{}));
            break;
        }
        case VMDomainIndexType::TxHashEVMToDVM: {
            view->ForEachVMDomainTxEdges(
                [&](const std::pair<VMDomainEdge, std::string> &index, const std::string &txHash) {
                    if (index.first == VMDomainEdge::EVMToDVM) {
                        indexesJson.pushKV(index.second, txHash);
                        ++count;
                    }
                    return true;
                },
                std::make_pair(VMDomainEdge::EVMToDVM, std::string{}));
            break;
        }
        default:
            throw JSONRPCError(RPC_INVALID_PARAMETER, "type out of range");
    }

    result.pushKV("indexes", indexesJson);
    result.pushKV("count", count);
    return result;
}

UniValue dumpevmdb(const JSONRPCRequest &request) {
    RPCHelpMan{
        "dumpevmdb",
        "Dump the full evm backend db for debugging.\n",
        {{
            "options",
            RPCArg::Type::OBJ,
            RPCArg::Optional::OMITTED,
            "",
            {
                {
                    "dumparg",
                    RPCArg::Type::STR,
                    RPCArg::Optional::OMITTED,
                    "Option to specify dump index",
                },
                {
                    "from",
                    RPCArg::Type::STR,
                    RPCArg::Optional::OMITTED,
                    "Specify starting key",
                },
                {
                    "limit",
                    RPCArg::Type::NUM,
                    RPCArg::Optional::OMITTED,
                    "Specify dump limit",
                },
            },
        }},
        RPCResult{"\"dbdump\"                  (string) The full evm backend db dump."
                  "This is for debugging purposes only.\n"},
        RPCExamples{HelpExampleCli("dumpevmdb", "'{\"dumparg\":\"all\", \"from\":<hex>, \"limit\":100}'")},
    }
        .Check(request);

    rust::string dumparg{};
    rust::string from{};
    rust::string limit{};
    CrossBoundaryResult result;
    if (request.params.size() == 1) {
        UniValue optionsObj = request.params[0].get_obj();
        RPCTypeCheckObj(optionsObj,
                        {
                            {"dumparg", UniValueType(UniValue::VSTR)},
                            {"from",    UniValueType(UniValue::VSTR)},
                            {"limit",   UniValueType(UniValue::VSTR)},
        },
                        true,
                        false);

        if (!optionsObj["dumparg"].isNull()) {
            const auto dumpargParam = optionsObj["dumparg"].get_str();
            dumparg = rs_try_from_utf8(result, ffi_from_string_to_slice(dumpargParam));
            if (!result.ok) {
                return JSONRPCError(RPC_INVALID_PARAMETER, "Invalid dumparg set, not UTF-8 valid");
            }
        }

        if (!optionsObj["from"].isNull()) {
            const auto fromParam = optionsObj["from"].get_str();
            from = rs_try_from_utf8(result, ffi_from_string_to_slice(fromParam));
            if (!result.ok) {
                return JSONRPCError(RPC_INVALID_PARAMETER, "Invalid from set, not UTF-8 valid");
            }
        }

        if (!optionsObj["limit"].isNull()) {
            const auto limitParam = optionsObj["limit"].get_str();
            limit = rs_try_from_utf8(result, ffi_from_string_to_slice(limitParam));
            if (!result.ok) {
                return JSONRPCError(RPC_INVALID_PARAMETER, "Invalid limit set, not UTF-8 valid");
            }
        }
    }

    const auto dumpResults = debug_dump_db(result, dumparg, from, limit);
    if (!result.ok) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Failed to get dumpdb logs: %s", result.reason.c_str()));
    }
    return std::string(dumpResults.data(), dumpResults.length());
}

UniValue logevmaccountstates(const JSONRPCRequest &request) {
    RPCHelpMan{
        "logevmaccountstates",
        "Log the full evm account states for debugging.\n",
        {},
        RPCResult{"\"accountstates\"                  (string) The full evm account states."
                  "This is for debugging purposes only.\n"},
        RPCExamples{HelpExampleCli("logevmaccountstates", "")},
    }
        .Check(request);

    auto &consensus = Params().GetConsensus();
    const CBlockIndex *tip = ::ChainActive().Tip();
    if (tip->nHeight < consensus.DF22MetachainHeight) {
        return "";
    }

    CrossBoundaryResult result;
    const auto dumpResults = debug_log_account_states(result);
    if (!result.ok) {
        throw JSONRPCError(RPC_MISC_ERROR, strprintf("Failed to log evm account states: %s", result.reason.c_str()));
    }
    return std::string(dumpResults.data(), dumpResults.length());
}

static const CRPCCommand commands[] = {
  //  category  name                   actor (function)      params
  //  --------- ---------------------- --------------------  ----------
    {"evm", "evmtx",               &evmtx,               {"from", "nonce", "gasPrice", "gasLimit", "to", "value", "data"}},
    {"evm", "vmmap",               &vmmap,               {"input", "type"}                                               },
    {"evm", "logvmmaps",           &logvmmaps,           {"type"}                                                        },
    {"evm", "dumpevmdb",           &dumpevmdb,           {"dumparg", "from", "limit"}                                    },
    {"evm", "logevmaccountstates", &logevmaccountstates, {}                                                              },
};

void RegisterEVMRPCCommands(CRPCTable &tableRPC) {
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++) {
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
    }
}
