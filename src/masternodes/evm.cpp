#include <masternodes/evm.h>

void CEvmDvmView::SetBlockHash(uint8_t type, uint256 blockHashKey, uint256 blockHash)
{
    WriteBy<BlockHash>(std::pair(type, blockHashKey), blockHash);
}

Res CEvmDvmView::GetBlockHash(uint8_t type, uint256 blockHashKey, uint256& blockHash) const
{
    if (ReadBy<BlockHash>(std::pair(type, blockHashKey), blockHash)) {
        return Res::Ok();        
    }
    else {
        return Res::Err("Block hash key does not exist.");
    }
}

Res CEvmDvmView::EraseBlockHash(uint8_t type, uint256 blockHashKey)
{
    EraseBy<BlockHash>(std::pair(type, blockHashKey));
    return Res::Ok();
}
