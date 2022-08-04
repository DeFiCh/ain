#include <DMCHandler.h>

#include <iostream>

bool DMCHandler::AddDMCPayloadToNativeBlock(std::shared_ptr<CBlock> block) {
  // Dummy imp for now
  block->dmcPayload = std::vector<unsigned char>({'\x44', '\x4D', '\x43', '\x20', '\x52', '\x4f', '\x43', '\x4b', '\x53'});
  return true;
}

bool DMCHandler::ConnectPayloadToDMC(const std::vector<unsigned char>& payload) {
  // Dummy imp for now
  std::cout << std::string(payload.begin(), payload.end()) << std::endl;
  return true;
}