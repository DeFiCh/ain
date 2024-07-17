#include <clientversion.h>
#include <dfi/accountshistory.h>
#include <dfi/customtx.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/mn_rpc.h>
#include <dfi/validation.h>
#include <dfi/vaulthistory.h>
#include <ffi/ffiexports.h>
#include <ffi/ffihelpers.h>
#include <httprpc.h>
#include <key_io.h>
#include <logging.h>
#include <net.h>
#include <util/system.h>

#include <algorithm>
#include <array>
#include <cstdint>

// TODO: Later switch this to u8 so we skip the
// conversion and is more efficient.
// Direct const* char ptr is not allowed due to CXX, but
// we can convert ourselves and pass the final u8.
void CppLogPrintf(rust::string message) {
    LogPrintf(message.c_str());
}

uint64_t getChainId() {
    return Params().GetConsensus().evmChainId;
}

bool isMining() {
    return gArgs.GetBoolArg("-gen", false);
}

rust::string publishEthTransaction(rust::Vec<uint8_t> rawTransaction) {
    std::vector<uint8_t> evmTx(rawTransaction.size());
    std::copy(rawTransaction.begin(), rawTransaction.end(), evmTx.begin());
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::EvmTx) << CEvmTxMessage{evmTx};

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    auto [view, accountView, vaultView] = GetSnapshots();
    auto targetHeight = view->GetLastHeight() + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vin.resize(2);
    rawTx.vin[0].scriptSig = CScript() << OP_0;
    rawTx.vin[1].scriptSig = CScript() << OP_0;

    rawTx.vout.emplace_back(0, scriptMeta);

    // check execution
    CTransactionRef optAuthTx;

    // TODO Replace execTestTx with non-throwing function
    try {
        execTestTx(CTransaction(rawTx), targetHeight, optAuthTx);
        send(MakeTransactionRef(std::move(rawTx)), optAuthTx)->GetHash().ToString();
    } catch (std::runtime_error &e) {
        return e.what();
    } catch (const UniValue &objError) {
        const auto obj = objError.get_obj();

        return obj["message"].get_str();
    }

    return {};
}

rust::vec<rust::string> getAccounts() {
    rust::vec<rust::string> addresses;
    const std::vector<std::shared_ptr<CWallet>> wallets = GetWallets();
    for (const std::shared_ptr<CWallet> &wallet : wallets) {
        for (auto &it : wallet->mapAddressBook) {
            if (std::holds_alternative<WitnessV16EthHash>(it.first)) {
                addresses.push_back(EncodeDestination(it.first));
            }
        }
    }
    return addresses;
}

rust::string getDatadir() {
#ifdef WIN32
    // https://learn.microsoft.com/en-us/cpp/cpp/char-wchar-t-char16-t-char32-t?view=msvc-170
    // We're sidestepping this for now unsafely making an assumption. Can crash on Windows
    // if odd paths are used. Require testing.
    return rust::String(reinterpret_cast<const char16_t *>(GetDataDir().c_str()));
#else
    return GetDataDir().c_str();
#endif
}

rust::string getNetwork() {
    return Params().NetworkIDString();
}

uint32_t getDifficulty(std::array<uint8_t, 32> blockHash) {
    uint256 hash{};
    std::copy(blockHash.begin(), blockHash.end(), hash.begin());

    const CBlockIndex *pblockindex;
    uint32_t difficulty{};
    {
        LOCK(cs_main);
        pblockindex = LookupBlockIndex(hash);

        if (!pblockindex) {
            return difficulty;
        }

        difficulty = pblockindex->nBits;
    }

    return difficulty;
}

std::array<uint8_t, 32> getChainWork(std::array<uint8_t, 32> blockHash) {
    uint256 hash{};
    std::copy(blockHash.begin(), blockHash.end(), hash.begin());

    const CBlockIndex *pblockindex;
    std::array<uint8_t, 32> chainWork{};
    {
        LOCK(cs_main);
        pblockindex = LookupBlockIndex(hash);

        if (!pblockindex) {
            return chainWork;
        }

        const auto sourceWork = ArithToUint256(pblockindex->nChainWork);
        std::copy(sourceWork.begin(), sourceWork.end(), chainWork.begin());
    }

    return chainWork;
}

rust::vec<TransactionData> getPoolTransactions() {
    std::multimap<int64_t, TransactionData> poolTransactionsByEntryTime;

    for (auto mi = mempool.mapTx.get<entry_time>().begin(); mi != mempool.mapTx.get<entry_time>().end(); ++mi) {
        const auto &tx = mi->GetTx();

        std::vector<unsigned char> metadata;
        const auto txType = GuessCustomTxType(tx, metadata, true);
        if (txType == CustomTxType::EvmTx) {
            CCustomTxMessage txMessage{CEvmTxMessage{}};
            const auto res =
                CustomMetadataParse(std::numeric_limits<uint32_t>::max(), Params().GetConsensus(), metadata, txMessage);
            if (!res) {
                continue;
            }

            const auto obj = std::get<CEvmTxMessage>(txMessage);
            poolTransactionsByEntryTime.emplace(mi->GetTime(),
                                                TransactionData{
                                                    static_cast<uint8_t>(TransactionDataTxType::EVM),
                                                    HexStr(obj.evmTx),
                                                    static_cast<uint8_t>(TransactionDataDirection::None),
                                                    mi->GetTime(),
                                                });
        } else if (txType == CustomTxType::TransferDomain) {
            CCustomTxMessage txMessage{CTransferDomainMessage{}};
            const auto res =
                CustomMetadataParse(std::numeric_limits<uint32_t>::max(), Params().GetConsensus(), metadata, txMessage);
            if (!res) {
                continue;
            }

            const auto obj = std::get<CTransferDomainMessage>(txMessage);
            if (obj.transfers.size() != 1) {
                continue;
            }

            if (obj.transfers[0].first.domain == static_cast<uint8_t>(VMDomain::DVM) &&
                obj.transfers[0].second.domain == static_cast<uint8_t>(VMDomain::EVM)) {
                poolTransactionsByEntryTime.emplace(mi->GetTime(),
                                                    TransactionData{
                                                        static_cast<uint8_t>(TransactionDataTxType::TransferDomain),
                                                        HexStr(obj.transfers[0].second.data),
                                                        static_cast<uint8_t>(TransactionDataDirection::DVMToEVM),
                                                        mi->GetTime(),
                                                    });
            } else if (obj.transfers[0].first.domain == static_cast<uint8_t>(VMDomain::EVM) &&
                       obj.transfers[0].second.domain == static_cast<uint8_t>(VMDomain::DVM)) {
                poolTransactionsByEntryTime.emplace(mi->GetTime(),
                                                    TransactionData{
                                                        static_cast<uint8_t>(TransactionDataTxType::TransferDomain),
                                                        HexStr(obj.transfers[0].first.data),
                                                        static_cast<uint8_t>(TransactionDataDirection::EVMToDVM),
                                                        mi->GetTime(),
                                                    });
            }
        }
    }

    rust::vec<TransactionData> poolTransactions;
    for (const auto &[key, txData] : poolTransactionsByEntryTime) {
        poolTransactions.push_back(txData);
    }

    return poolTransactions;
}

uint64_t getNativeTxSize(rust::Vec<uint8_t> rawTransaction) {
    std::vector<uint8_t> evmTx(rawTransaction.size());
    std::copy(rawTransaction.begin(), rawTransaction.end(), evmTx.begin());
    CDataStream metadata(DfTxMarker, SER_NETWORK, PROTOCOL_VERSION);
    metadata << static_cast<unsigned char>(CustomTxType::EvmTx) << CEvmTxMessage{evmTx};

    CScript scriptMeta;
    scriptMeta << OP_RETURN << ToByteVector(metadata);

    auto [view, accountView, vaultView] = GetSnapshots();
    auto targetHeight = view->GetLastHeight() + 1;

    const auto txVersion = GetTransactionVersion(targetHeight);
    CMutableTransaction rawTx(txVersion);

    rawTx.vin.resize(2);
    rawTx.vin[0].scriptSig = CScript() << OP_0;
    rawTx.vin[1].scriptSig = CScript() << OP_0;

    rawTx.vout.emplace_back(0, scriptMeta);

    CTransaction tx(rawTx);

    return tx.GetTotalSize();
}

uint64_t getMinRelayTxFee() {
    return ::minRelayTxFee.GetFeePerK() * 10000000;
}

std::array<uint8_t, 32> getEthPrivKey(EvmAddressData key) {
    CKey ethPrivKey;
    const auto ethKeyID = CKeyID(uint160::FromByteArray(key));
    for (const auto &wallet : GetWallets()) {
        if (wallet->GetKey(ethKeyID, ethPrivKey)) {
            std::array<uint8_t, 32> privKeyArray{};
            std::copy(ethPrivKey.begin(), ethPrivKey.end(), privKeyArray.begin());
            return privKeyArray;
        }
    }
    return {};
}

rust::string getStateInputJSON() {
    return gArgs.GetArg("-ethstartstate", "");
}

// Returns Major, Minor, Revision in format: "X.Y.Z"
rust::string getClientVersion() {
    return rust::String(FormatVersionAndSuffix());
}

std::array<int64_t, 2> getEthSyncStatus() {
    auto [view, accountView, vaultView] = GetSnapshots();

    const auto viewHeight = view->GetLastHeight();
    auto currentHeight = viewHeight ? viewHeight : -1;
    auto highestBlock =
        pindexBestHeader ? pindexBestHeader->nHeight : viewHeight;  // return current block count if no peers

    return {currentHeight, highestBlock};
}

Attributes getAttributeValues(std::size_t mnview_ptr) {
    auto val = Attributes::Default();

    LOCK(cs_main);
    auto view = reinterpret_cast<CCustomCSView *>(static_cast<uintptr_t>(mnview_ptr));
    if (!view) {
        view = pcustomcsview.get();
    }

    const auto attributes = view->GetAttributes();

    CDataStructureV0 blockGasTargetFactorKey{AttributeTypes::EVMType, EVMIDs::Block, EVMKeys::GasTargetFactor};
    CDataStructureV0 blockGasLimitKey{AttributeTypes::EVMType, EVMIDs::Block, EVMKeys::GasLimit};
    CDataStructureV0 finalityCountKey{AttributeTypes::EVMType, EVMIDs::Block, EVMKeys::Finalized};
    CDataStructureV0 rbfIncrementMinPctKey{AttributeTypes::EVMType, EVMIDs::Block, EVMKeys::RbfIncrementMinPct};

    if (attributes->CheckKey(blockGasTargetFactorKey)) {
        val.blockGasTargetFactor = attributes->GetValue(blockGasTargetFactorKey, DEFAULT_EVM_BLOCK_GAS_TARGET_FACTOR);
    }
    if (attributes->CheckKey(blockGasLimitKey)) {
        val.blockGasLimit = attributes->GetValue(blockGasLimitKey, DEFAULT_EVM_BLOCK_GAS_LIMIT);
    }
    if (attributes->CheckKey(finalityCountKey)) {
        val.finalityCount = attributes->GetValue(finalityCountKey, DEFAULT_EVM_FINALITY_COUNT);
    }
    if (attributes->CheckKey(rbfIncrementMinPctKey)) {
        val.rbfIncrementMinPct = attributes->GetValue(rbfIncrementMinPctKey, DEFAULT_EVM_RBF_FEE_INCREMENT);
    }

    return val;
}

uint32_t getEthMaxConnections() {
    return gArgs.GetArg("-ethmaxconnections", DEFAULT_ETH_MAX_CONNECTIONS);
}

void printEVMPortUsage(const uint8_t portType, const uint16_t portNumber) {
    return SetPortToLockFile(static_cast<AutoPort>(portType), portNumber);
}

uint32_t getEthMaxResponseByteSize() {
    const auto max_response_size_mb = gArgs.GetArg("-ethmaxresponsesize", DEFAULT_ETH_MAX_RESPONSE_SIZE_MB);
    return max_response_size_mb * 1024 * 1024;
}

uint32_t getEthTracingMaxMemoryUsageBytes() {
    return gArgs.GetArg("-ethtracingmaxmemoryusage", DEFAULT_TRACING_RAW_MAX_MEMORY_USAGE_BYTES);
}

int64_t getSuggestedPriorityFeePercentile() {
    return gArgs.GetArg("-evmtxpriorityfeepercentile", DEFAULT_SUGGESTED_PRIORITY_FEE_PERCENTILE);
}

uint64_t getEstimateGasErrorRatio() {
    return gArgs.GetArg("-evmestimategaserrorratio", DEFAULT_ESTIMATE_GAS_ERROR_RATIO);
}

bool getDST20Tokens(std::size_t mnview_ptr, rust::vec<DST20Token> &tokens) {
    LOCK(cs_main);

    bool res = true;
    CCustomCSView *cache = reinterpret_cast<CCustomCSView *>(static_cast<uintptr_t>(mnview_ptr));
    cache->ForEachToken(
        [&](DCT_ID const &id, CTokensView::CTokenImpl token) {
            if (!token.IsDAT() || token.IsPoolShare()) {
                return true;
            }
            if (token.name.size() > CToken::POST_METACHAIN_TOKEN_NAME_BYTE_SIZE) {
                res = false;
                return false;
            }

            CrossBoundaryResult result;
            auto token_name = rs_try_from_utf8(result, ffi_from_string_to_slice(token.name));
            if (!result.ok) {
                LogPrintf("Error migrating DST20 token, token name not valid UTF-8\n");
                res = false;
                return false;
            }
            auto token_symbol = rs_try_from_utf8(result, ffi_from_string_to_slice(token.symbol));
            if (!result.ok) {
                LogPrintf("Error migrating DST20 token, token symbol not valid UTF-8\n");
                res = false;
                return false;
            }
            tokens.push_back({id.v, token_name, token_symbol});
            return true;
        },
        DCT_ID{1});  // start from non-DFI
    return res;
}

int32_t getNumCores() {
    const auto n = GetNumCores() - 1;
    return std::max(1, n);
}

rust::string getCORSAllowedOrigin() {
    return gArgs.GetArg("-rpcallowcors", "");
}

int32_t getNumConnections() {
    return (int32_t)g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL);
}

size_t getEccLruCacheCount() {
    return gArgs.GetArg("-ecclrucache", DEFAULT_ECC_LRU_CACHE_COUNT);
}

size_t getEvmValidationLruCacheCount() {
    return gArgs.GetArg("-evmvlrucache", DEFAULT_EVMV_LRU_CACHE_COUNT);
}

size_t getEvmNotificationChannelBufferSize() {
    return gArgs.GetArg("-evmnotificationchannel", DEFAULT_EVM_NOTIFICATION_CHANNEL_BUFFER_SIZE);
}

bool isEthDebugRPCEnabled() {
    return gArgs.GetBoolArg("-ethdebug", DEFAULT_ETH_DEBUG_ENABLED);
}

bool isEthDebugTraceRPCEnabled() {
    return gArgs.GetBoolArg("-ethdebugtrace", DEFAULT_ETH_DEBUG_TRACE_ENABLED);
}

rust::vec<SystemTxData> getEVMSystemTxsFromBlock(std::array<uint8_t, 32> evmBlockHash) {
    LOCK(cs_main);

    rust::vec<SystemTxData> out;
    auto blockHash =
        pcustomcsview->GetVMDomainBlockEdge(VMDomainEdge::EVMToDVM, uint256::FromByteArray(evmBlockHash).GetHex());
    if (!blockHash.val.has_value()) {
        return out;
    }
    auto hash = uint256S(*blockHash);
    const auto consensus = Params().GetConsensus();
    const CBlockIndex *pblockindex = LookupBlockIndex(hash);
    if (!pblockindex) {
        return out;
    }
    CBlock block;
    if (IsBlockPruned(pblockindex)) {
        return out;
    }
    if (!ReadBlockFromDisk(block, pblockindex, consensus)) {
        // Block not found on disk. This could be because we have the block
        // header in our index but don't have the block (for example if a
        // non-whitelisted node sends us an unrequested long chain of valid
        // blocks, we add the headers to our index, but don't accept the
        // block).
        return out;
    }
    for (const auto &tx : block.vtx) {
        std::vector<unsigned char> metadata;
        auto txType = GuessCustomTxType(*tx, metadata, true);
        if (txType == CustomTxType::EvmTx) {
            out.push_back(SystemTxData{
                SystemTxType::EVMTx, {0, {}, {}}
            });
        } else if (txType == CustomTxType::TransferDomain) {
            auto txMessage = customTypeToMessage(CustomTxType::TransferDomain);
            auto res = CustomMetadataParse(block.deprecatedHeight, consensus, metadata, txMessage);
            if (!res) {
                return out;
            }
            const auto obj = std::get<CTransferDomainMessage>(txMessage);
            for (const auto &[src, dst] : obj.transfers) {
                auto tokenId = src.amount.nTokenId;
                if (tokenId != DCT_ID{0}) {
                    if (src.domain == static_cast<uint8_t>(VMDomain::DVM) &&
                        dst.domain == static_cast<uint8_t>(VMDomain::EVM)) {
                        out.push_back(SystemTxData{
                            SystemTxType::DST20BridgeIn, {tokenId.v, {}, {}}
                        });
                    } else if (src.domain == static_cast<uint8_t>(VMDomain::EVM) &&
                               dst.domain == static_cast<uint8_t>(VMDomain::DVM)) {
                        out.push_back(SystemTxData{
                            SystemTxType::DST20BridgeOut, {tokenId.v, {}, {}}
                        });
                    }
                } else {
                    if (src.domain == static_cast<uint8_t>(VMDomain::DVM) &&
                        dst.domain == static_cast<uint8_t>(VMDomain::EVM)) {
                        out.push_back(SystemTxData{
                            SystemTxType::TransferDomainIn, {0, {}, {}}
                        });
                    } else if (src.domain == static_cast<uint8_t>(VMDomain::EVM) &&
                               dst.domain == static_cast<uint8_t>(VMDomain::DVM)) {
                        out.push_back(SystemTxData{
                            SystemTxType::TransferDomainOut, {0, {}, {}}
                        });
                    }
                }
            }
        } else if (txType == CustomTxType::CreateToken) {
            auto creationTx = tx->GetHash();
            auto res = pcustomcsview->GetTokenByCreationTx(creationTx);
            if (!res) {
                return out;
            }
            auto id = res->first;
            auto token = res->second;
            CrossBoundaryResult result;
            auto token_name = rs_try_from_utf8(result, ffi_from_string_to_slice(token.name));
            if (!result.ok) {
                return out;
            }
            auto token_symbol = rs_try_from_utf8(result, ffi_from_string_to_slice(token.symbol));
            if (!result.ok) {
                return out;
            }
            out.push_back(SystemTxData{
                SystemTxType::DeployContract, {id.v, token_name, token_symbol}
            });
        } else if (txType == CustomTxType::UpdateTokenAny) {
            auto txMessage = customTypeToMessage(CustomTxType::UpdateTokenAny);
            auto res = CustomMetadataParse(block.deprecatedHeight, consensus, metadata, txMessage);
            if (!res) {
                return out;
            }
            const auto obj = std::get<CUpdateTokenMessage>(txMessage);
            auto pair = pcustomcsview->GetTokenByCreationTx(obj.tokenTx);
            if (!pair) {
                return out;
            }
            auto id = pair->first;
            auto token = pair->second;
            if (!token.IsDAT()) {
                return out;
            }
            CrossBoundaryResult result;
            auto token_name = rs_try_from_utf8(result, ffi_from_string_to_slice(obj.token.name));
            if (!result.ok) {
                return out;
            }
            auto token_symbol = rs_try_from_utf8(result, ffi_from_string_to_slice(obj.token.symbol));
            if (!result.ok) {
                return out;
            }
            out.push_back(SystemTxData{
                SystemTxType::UpdateContractName, {id.v, token_name, token_symbol}
            });
        }
    }
    return out;
}

uint64_t getDF23Height() {
    return Params().GetConsensus().DF23Height;
}

bool migrateTokensFromEVM(std::size_t mnview_ptr, TokenAmount old_amount, TokenAmount &new_amount) {
    return ExecuteTokenMigrationEVM(mnview_ptr, old_amount, new_amount);
}
