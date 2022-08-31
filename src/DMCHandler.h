#ifndef DEFI_DMC_HANDLER_H
#define DEFI_DMC_HANDLER_H

#include <iostream>
#include <primitives/block.h>
#include "rpc/client.h"

class DFITx {
    private:
    char* from; // TODO: get correct DMC Address type
    char* to; // TODO: get correct DFI address type
    int64_t amount;
    char* signature;

public:
    DFITx(char* from, char* to, int64_t amount, char* signature){
        this->from = from;
        this->to = to;
        this->amount = amount;
        this->signature = signature;
    }
};

class DMCTx {
private:
    char* from; // TODO: get correct DFI address type
    char* to; // TODO: get correct DMC Address type
    int64_t amount;
    char* signature;

public:
    DMCTx(char* from, char* to, int64_t amount, char* signature){
        this->from = from;
        this->to = to;
        this->amount = amount;
        this->signature = signature;
    }
};

struct EncodedDMCBlock {
    std::vector<unsigned char> blockdata;
} EncodedDMCBlock;

// WIP
class DMCHandler {
private:
    rpc::client DMCNode;
    void InitializeRPCClient();
    
public:
    bool AddDMCPayloadToNativeBlock(std::shared_ptr<CBlock> block, std::vector<DMCTx> txN);
    bool ConnectPayloadToDMC(const std::vector<unsigned char>& payload);
};

#endif // DEFI_DMC_HANDLER_H