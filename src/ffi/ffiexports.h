#ifndef DEFI_FFI_FFIEXPORTS_H
#define DEFI_FFI_FFIEXPORTS_H

#include <chainparams.h>
#include <ffi/cxx.h>

// Defaults for attributes relating to EVM functionality
static constexpr uint64_t DEFAULT_EVM_BLOCK_GAS_TARGET = 15000000;
static constexpr uint64_t DEFAULT_EVM_BLOCK_GAS_LIMIT = 30000000;
static constexpr uint64_t DEFAULT_EVM_FINALITY_COUNT = 100;
static constexpr uint32_t DEFAULT_ETH_MAX_CONNECTIONS = 100;

struct Attributes {
    uint64_t blockGasTarget;
    uint64_t blockGasLimit;
    uint64_t finalityCount;

    static Attributes Default() {
        return Attributes {
                DEFAULT_EVM_BLOCK_GAS_TARGET,
                DEFAULT_EVM_BLOCK_GAS_LIMIT,
                DEFAULT_EVM_FINALITY_COUNT,
        };
    }
};

struct DST20Token {
    uint64_t id;
    rust::string name;
    rust::string symbol;
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
rust::vec<rust::string> getPoolTransactions();
uint64_t getNativeTxSize(rust::Vec<uint8_t> rawTransaction);
uint64_t getMinRelayTxFee();
std::array<uint8_t, 32> getEthPrivKey(rust::string key);
rust::string getStateInputJSON();
int getHighestBlock();
int getCurrentHeight();
Attributes getAttributeDefaults();
void CppLogPrintf(rust::string message);
rust::vec<DST20Token> getDST20Tokens(std::size_t mnview_ptr);

#endif  // DEFI_FFI_FFIEXPORTS_H
