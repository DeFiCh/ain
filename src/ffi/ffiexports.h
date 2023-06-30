#ifndef DEFI_FFI_FFIEXPORTS_H
#define DEFI_FFI_FFIEXPORTS_H

#include <chainparams.h>
#include <ffi/cxx.h>

uint64_t getChainId();
bool isMining();
rust::string publishEthTransaction(rust::Vec<uint8_t> rawTransaction);
rust::vec<rust::string> getAccounts();
rust::string getDatadir();
rust::string getNetwork();
uint32_t getDifficulty(std::array<uint8_t, 32> blockHash);
std::array<uint8_t, 32> getChainWork(std::array<uint8_t, 32> blockHash);
rust::vec<rust::string> getPoolTransactions();
uint64_t getNativeTxSize(rust::Vec<uint8_t> rawTransaction);
uint64_t getMinRelayTxFee();
std::array<uint8_t, 32> getEthPrivKey(std::array<uint8_t, 20> keyID);
rust::string getStateInputJSON();
int getHighestBlock();
int getCurrentHeight();
bool pastChangiIntermediateHeight2();
bool pastChangiIntermediateHeight3();
bool pastChangiIntermediateHeight4();

#endif  // DEFI_FFI_FFIEXPORTS_H
