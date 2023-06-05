#include <masternodes/evm.h>

uint256 CEvmDvmView::GetBlockHash(uint8_t type, uint256 blockHashKey) const
{
    uint256 blockhash;
    if (ReadBy<BlockHash>(std::pair(type, blockHashKey), blockhash))
        return blockhash;
    return uint256();
}

void CEvmDvmView::SetBlockHash(uint8_t type, uint256 blockHashKey, uint256 blockHash)
{
    WriteBy<BlockHash>(std::pair(type, blockHashKey), blockHash);
}

Res CEvmDvmView::EraseBlockHash(uint8_t type, uint256 blockHashKey)
{
    EraseBy<BlockHash>(std::pair(type, blockHashKey));
    return Res::Ok();
}
