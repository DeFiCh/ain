#ifndef DEFI_FFI_FFIEXPORTS_H
#define DEFI_FFI_FFIEXPORTS_H

#include <chainparams.h>
#include <ffi/cxx.h>
#include <httprpc.h>

// Defaults for attributes relating to EVM functionality
static constexpr uint64_t DEFAULT_EVM_BLOCK_GAS_TARGET_FACTOR = 2;
static constexpr uint64_t DEFAULT_EVM_BLOCK_GAS_LIMIT = 30000000;
static constexpr uint64_t DEFAULT_EVM_FINALITY_COUNT = 100;
static constexpr CAmount DEFAULT_EVM_RBF_FEE_INCREMENT = COIN / 10;

// Defaults for attributes relating to networking
static constexpr uint32_t DEFAULT_ETH_MAX_CONNECTIONS = 100;
static constexpr uint32_t DEFAULT_ETH_MAX_RESPONSE_SIZE_MB = 25;  // 25 megabytes

static constexpr uint32_t DEFAULT_ECC_LRU_CACHE_COUNT = 10000;
static constexpr uint32_t DEFAULT_EVMV_LRU_CACHE_COUNT = 10000;

static constexpr bool DEFAULT_ETH_DEBUG_ENABLED = false;
static constexpr bool DEFAULT_ETH_DEBUG_TRACE_ENABLED = true;

static constexpr bool DEFAULT_OCEAN_ARCHIVE_ENABLED = false;

struct Attributes {
    uint64_t blockGasTargetFactor;
    uint64_t blockGasLimit;
    uint64_t finalityCount;
    uint64_t rbfIncrementMinPct;

    static Attributes Default() {
        return Attributes{
            DEFAULT_EVM_BLOCK_GAS_TARGET_FACTOR,
            DEFAULT_EVM_BLOCK_GAS_LIMIT,
            DEFAULT_EVM_FINALITY_COUNT,
            DEFAULT_EVM_RBF_FEE_INCREMENT,
        };
    }
};

struct DST20Token {
    uint64_t id;
    rust::string name;
    rust::string symbol;
};

struct TransactionData {
    uint8_t txType;
    rust::string data;
    uint8_t direction;
};

enum class TransactionDataTxType : uint8_t {
    EVM,
    TransferDomain,
};

enum class TransactionDataDirection : uint8_t {
    None,
    DVMToEVM,
    EVMToDVM,
};

uint64_t getChainId();
int getRPCPort();
rust::string getRPCAuth();
bool isMining();
rust::string publishEthTransaction(rust::Vec<uint8_t> rawTransaction);
rust::vec<rust::string> getAccounts();
rust::string getDatadir();
rust::string getNetwork();
uint32_t getDifficulty(std::array<uint8_t, 32> blockHash);
uint32_t getEthMaxConnections();
uint32_t getEthMaxResponseByteSize();
std::array<uint8_t, 32> getChainWork(std::array<uint8_t, 32> blockHash);
rust::vec<TransactionData> getPoolTransactions();
uint64_t getNativeTxSize(rust::Vec<uint8_t> rawTransaction);
uint64_t getMinRelayTxFee();
std::array<uint8_t, 32> getEthPrivKey(rust::string key);
rust::string getStateInputJSON();
std::array<int64_t, 2> getEthSyncStatus();
Attributes getAttributeValues(std::size_t mnview_ptr);
void CppLogPrintf(rust::string message);
bool getDST20Tokens(std::size_t mnview_ptr, rust::vec<DST20Token> &tokens);
rust::string getClientVersion();
int32_t getNumCores();
rust::string getCORSAllowedOrigin();
int32_t getNumConnections();
size_t getEccLruCacheCount();
size_t getEvmValidationLruCacheCount();
bool isEthDebugRPCEnabled();
bool isEthDebugTraceRPCEnabled();
bool isOceanEnabled();

#endif  // DEFI_FFI_FFIEXPORTS_H
