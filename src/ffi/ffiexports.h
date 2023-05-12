#ifndef DEFI_FFI_FFIEXPORTS_H
#define DEFI_FFI_FFIEXPORTS_H

#include <chainparams.h>
#include <key.h>
#include <ffi/cxx.h>

std::array<uint8_t, 32> getPrivKey(rust::string fromAddress);
uint64_t getChainId();
bool isMining();
bool publishEthTransaction(rust::Vec<uint8_t> rawTransaction);
rust::vec<rust::string> getAccounts();
rust::string getDatadir();
uint32_t getDifficulty(std::array<uint8_t, 32> blockHash);
std::array<uint8_t, 32> getChainWork(std::array<uint8_t, 32> blockHash);
rust::vec<rust::string> getPoolTransactions();
uint64_t getNativeTxSize(rust::Vec<uint8_t> rawTransaction);
uint64_t getMinRelayTxFee();

#endif // DEFI_FFI_FFIEXPORTS_H
