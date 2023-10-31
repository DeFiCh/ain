#ifndef DEFI_FFI_FFIEXPORTS_H
#define DEFI_FFI_FFIEXPORTS_H

#include <chainparams.h>
#include <ffi/cxx.h>
#include <httprpc.h>

// Defaults for attributes relating to EVM functionality
static constexpr uint64_t DEFAULT_EVM_BLOCK_GAS_TARGET = 15000000;
static constexpr uint64_t DEFAULT_EVM_BLOCK_GAS_LIMIT = 30000000;
static constexpr uint64_t DEFAULT_EVM_FINALITY_COUNT = 100;
static constexpr uint64_t DEFAULT_EVM_RBF_FEE_INCREMENT = COIN / 10;
static constexpr uint32_t DEFAULT_ETH_MAX_CONNECTIONS = 100;

static constexpr uint32_t DEFAULT_ECC_LRU_CACHE_COUNT = 10000;
static constexpr uint32_t DEFAULT_EVMV_LRU_CACHE_COUNT = 10000;

static constexpr bool DEFAULT_ETH_DEBUG_ENABLED = false;
static constexpr bool DEFAULT_ETH_DEBUG_TRACE_ENABLED = true;

struct Attributes {
    uint64_t blockGasTarget;
    uint64_t blockGasLimit;
    uint64_t finalityCount;
    uint64_t rbfFeeIncrement;

    static Attributes Default() {
        return Attributes {
                DEFAULT_EVM_BLOCK_GAS_TARGET,
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
bool isMining();
rust::string publishEthTransaction(rust::Vec<uint8_t> rawTransaction);
rust::vec<rust::string> getAccounts();
rust::string getDatadir();
rust::string getNetwork();
uint32_t getDifficulty(std::array<uint8_t, 32> blockHash);
uint32_t getEthMaxConnections();
std::array<uint8_t, 32> getChainWork(std::array<uint8_t, 32> blockHash);
rust::vec<TransactionData> getPoolTransactions();
uint64_t getNativeTxSize(rust::Vec<uint8_t> rawTransaction);
uint64_t getMinRelayTxFee();
std::array<uint8_t, 32> getEthPrivKey(rust::string key);
rust::string getStateInputJSON();
int getHighestBlock();
int getCurrentHeight();
Attributes getAttributeDefaults(std::size_t mnview_ptr);
void CppLogPrintf(rust::string message);
rust::vec<DST20Token> getDST20Tokens(std::size_t mnview_ptr);
rust::string getClientVersion();
int32_t getNumCores();
rust::string getCORSAllowedOrigin();
int32_t getNumConnections();
size_t getEccLruCacheCount();
size_t getEvmValidationLruCacheCount();
bool isEthDebugRPCEnabled();
bool isEthDebugTraceRPCEnabled();

#endif  // DEFI_FFI_FFIEXPORTS_H
