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

// Defaults for attributes relating to tracing
static constexpr uint32_t DEFAULT_TRACING_RAW_MAX_MEMORY_USAGE_BYTES = 20000000;

// Default for attributes relating to gasprice setting
static constexpr int64_t DEFAULT_SUGGESTED_PRIORITY_FEE_PERCENTILE = 60;

// Default for attributes relating to gasprice setting
static constexpr uint64_t DEFAULT_ESTIMATE_GAS_ERROR_RATIO = 15;

static constexpr uint32_t DEFAULT_ECC_LRU_CACHE_COUNT = 10000;
static constexpr uint32_t DEFAULT_EVMV_LRU_CACHE_COUNT = 10000;
static constexpr uint32_t DEFAULT_EVM_NOTIFICATION_CHANNEL_BUFFER_SIZE = 10000;

static constexpr bool DEFAULT_ETH_DEBUG_ENABLED = false;
static constexpr bool DEFAULT_ETH_DEBUG_TRACE_ENABLED = true;
static constexpr bool DEFAULT_ETH_SUBSCRIPTION_ENABLED = true;

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
    int64_t entryTime;
};

struct TokenAmount {
    uint32_t id;
    uint64_t amount;
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

enum class SystemTxType : uint8_t {
    EVMTx = 0,
    TransferDomainIn = 1,
    TransferDomainOut = 2,
    DST20BridgeIn = 3,
    DST20BridgeOut = 4,
    DeployContract = 5,
    UpdateContractName = 6,
};

struct SystemTxData {
    SystemTxType txType;
    DST20Token token;
};

uint64_t getChainId();
bool isMining();
rust::string publishEthTransaction(rust::Vec<uint8_t> rawTransaction);
rust::vec<rust::string> getAccounts();
rust::string getDatadir();
rust::string getNetwork();
uint32_t getDifficulty(std::array<uint8_t, 32> blockHash);
uint32_t getEthMaxConnections();
void printEVMPortUsage(const uint8_t portType, const uint16_t portNumber);
uint32_t getEthMaxResponseByteSize();
uint32_t getEthTracingMaxMemoryUsageBytes();
int64_t getSuggestedPriorityFeePercentile();
uint64_t getEstimateGasErrorRatio();
std::array<uint8_t, 32> getChainWork(std::array<uint8_t, 32> blockHash);
rust::vec<TransactionData> getPoolTransactions();
uint64_t getNativeTxSize(rust::Vec<uint8_t> rawTransaction);
uint64_t getMinRelayTxFee();
std::array<uint8_t, 32> getEthPrivKey(EvmAddressData key);
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
size_t getEvmNotificationChannelBufferSize();
bool isEthDebugRPCEnabled();
bool isEthDebugTraceRPCEnabled();
// Gets all EVM system txs and their respective types from DVM block.
rust::vec<SystemTxData> getEVMSystemTxsFromBlock(std::array<uint8_t, 32> evmBlockHash);
uint64_t getDF23Height();
bool migrateTokensFromEVM(std::size_t mnview_ptr, TokenAmount old_amount, TokenAmount &new_amount);

#endif  // DEFI_FFI_FFIEXPORTS_H
