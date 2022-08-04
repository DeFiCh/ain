#ifndef DEFI_DMC_HANDLER_H
#define DEFI_DMC_HANDLER_H

#include <iostream>
#include <primitives/block.h>

// WIP
class DMCHandler {
public:
    bool AddDMCPayloadToNativeBlock(std::shared_ptr<CBlock> block);
    bool ConnectPayloadToDMC(const std::vector<unsigned char>& payload);
};

#endif // DEFI_DMC_HANDLER_H