#ifndef DEFI_EVM_FFI_H
#define DEFI_EVM_FFI_H

#include <chainparams.h>
#include <ffi/cxx.h>

uint64_t getChainId();
bool isMining();
bool publishEthTransaction(rust::Vec<uint8_t> rawTransaction);
rust::vec<rust::string> getAccounts();
rust::string getDatadir();
uint32_t getDifficulty(std::array<uint8_t, 32> blockHash);
std::array<uint8_t, 32> getChainWork(std::array<uint8_t, 32> blockHash);

#endif // DEFI_EVM_FFI_H
