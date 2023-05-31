#include <masternodes/evm.h>

uint256 CEvmDvmView::GetBlockHash(uint8_t type, uint256 blockHash) const
{
    uint256 hash;
    if (ReadBy<BlockHash>(std::pair(type, blockHash), hash))
        return hash;
    return uint256();
}

void CEvmDvmView::SetBlockHash(uint8_t type, uint256 evmHash, uint256 dvmHash)
{
    WriteBy<BlockHash>(std::pair(type ,evmHash), dvmHash);
}
