#include <DMCHandler.h>
#include <logging.h>

#include "rpc/client.h"

#include <iostream>

void DMCHandler::InitializeRPCClient(){
  // Add the correct port number
  this->DMCNode("localhost", 8080);
}

bool DMCHandler::AddDMCPayloadToNativeBlock(std::shared_ptr<CBlock> block, std::vector<DMCTx> txN) {
  // Get DMC node to mint the new block
  EncodedDMCBlock newDMCBlock = nullptr;
  newDMCBlock = (this->DMCNode).call("mintBlock", txN);
  if(newDMCBlock == nullptr){
    // TODO: add exponential backoff
    newDMCBlock = (this->DMCNode).call("getBlock");
  }
  block->dmcPayload = newDMCBlock;
  
  return true;
}

bool DMCHandler::ConnectPayloadToDMC(const std::vector<unsigned char>& payload) {
  // Dummy imp for now
  LogPrintf("DMC Payload: [%s]", std::string(payload.begin(), payload.end()));
  (this->DMCNode).call("connectBlock", payload);
  return true;
}