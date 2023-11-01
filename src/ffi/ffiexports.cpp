#include <clientversion.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/mn_rpc.h>
#include <ffi/ffiexports.h>
#include <httprpc.h>
#include <key_io.h>
#include <logging.h>
#include <net.h>
#include <util/system.h>
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

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

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
    std::multimap<uint64_t, TransactionData> poolTransactionsByFee;

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
            poolTransactionsByFee.emplace(mi->GetEVMRbfMinTipFee(),
                                          TransactionData{
                                              static_cast<uint8_t>(TransactionDataTxType::EVM),
                                              HexStr(obj.evmTx),
                                              static_cast<uint8_t>(TransactionDataDirection::None),
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
                poolTransactionsByFee.emplace(mi->GetEVMRbfMinTipFee(),
                                              TransactionData{
                                                  static_cast<uint8_t>(TransactionDataTxType::TransferDomain),
                                                  HexStr(obj.transfers[0].second.data),
                                                  static_cast<uint8_t>(TransactionDataDirection::DVMToEVM),
                                              });
            } else if (obj.transfers[0].first.domain == static_cast<uint8_t>(VMDomain::EVM) &&
                       obj.transfers[0].second.domain == static_cast<uint8_t>(VMDomain::DVM)) {
                poolTransactionsByFee.emplace(mi->GetEVMRbfMinTipFee(),
                                              TransactionData{
                                                  static_cast<uint8_t>(TransactionDataTxType::TransferDomain),
                                                  HexStr(obj.transfers[0].first.data),
                                                  static_cast<uint8_t>(TransactionDataDirection::EVMToDVM),
                                              });
            }
        }
    }

    rust::vec<TransactionData> poolTransactions;
    for (const auto &[key, txData] : poolTransactionsByFee) {
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

    int targetHeight;
    {
        LOCK(cs_main);
        targetHeight = ::ChainActive().Height() + 1;
    }

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

std::array<uint8_t, 32> getEthPrivKey(rust::string key) {
    const auto dest = DecodeDestination(std::string(key.begin(), key.length()));
    if (dest.index() != WitV16KeyEthHashType) {
        return {};
    }
    const auto keyID = std::get<WitnessV16EthHash>(dest);
    const CKeyID ethKeyID{keyID};

    CKey ethPrivKey;
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
    LOCK(cs_main);

    auto currentHeight = ::ChainActive().Height() ? (int)::ChainActive().Height() : -1;
    auto highestBlock = pindexBestHeader ? pindexBestHeader->nHeight
                                         : (int)::ChainActive().Height();  // return current block count if no peers

    return std::array<int64_t, 2>{currentHeight, highestBlock};
}

Attributes getAttributeValues(std::size_t mnview_ptr) {
    auto val = Attributes::Default();

    LOCK(cs_main);
    auto view = reinterpret_cast<CCustomCSView *>(static_cast<uintptr_t>(mnview_ptr));
    if (!view) {
        view = pcustomcsview.get();
    }

    const auto attributes = view->GetAttributes();

    CDataStructureV0 blockGasLimitMultiplierKey{AttributeTypes::EVMType, EVMIDs::Block, EVMKeys::GasLimitMultipler};
    CDataStructureV0 blockGasLimitKey{AttributeTypes::EVMType, EVMIDs::Block, EVMKeys::GasLimit};
    CDataStructureV0 finalityCountKey{AttributeTypes::EVMType, EVMIDs::Block, EVMKeys::Finalized};
    CDataStructureV0 rbfIncrementMinPctKey{AttributeTypes::EVMType, EVMIDs::Block, EVMKeys::RbfIncrementMinPct};

    if (attributes->CheckKey(blockGasLimitMultiplierKey)) {
        val.blockGasLimitMultiplier =
            attributes->GetValue(blockGasLimitMultiplierKey, DEFAULT_EVM_BLOCK_GAS_TARGET_MULTIPLIER);
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

rust::vec<DST20Token> getDST20Tokens(std::size_t mnview_ptr) {
    LOCK(cs_main);

    rust::vec<DST20Token> tokens;
    CCustomCSView *cache = reinterpret_cast<CCustomCSView *>(static_cast<uintptr_t>(mnview_ptr));
    cache->ForEachToken(
        [&](DCT_ID const &id, CTokensView::CTokenImpl token) {
            if (!token.IsDAT() || token.IsPoolShare()) {
                return true;
            }

            tokens.push_back({id.v, token.name, token.symbol});
            return true;
        },
        DCT_ID{1});  // start from non-DFI
    return tokens;
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

bool isEthDebugRPCEnabled() {
    return gArgs.GetBoolArg("-ethdebug", DEFAULT_ETH_DEBUG_ENABLED);
}

bool isEthDebugTraceRPCEnabled() {
    return gArgs.GetBoolArg("-ethdebugtrace", DEFAULT_ETH_DEBUG_TRACE_ENABLED);
}
