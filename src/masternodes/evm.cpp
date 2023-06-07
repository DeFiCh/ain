#include <masternodes/evm.h>

Res CEvmDvmView::SetBlockHash(uint8_t type, uint256 blockHashKey, uint256 blockHash)
{
    Require(WriteBy<BlockHash>(std::pair(type, blockHashKey), blockHash), [=]{ return strprintf("Failed to store block hash %s to database", blockHashKey.GetHex()); });
    return Res::Ok();
}

Res CEvmDvmView::EraseBlockHash(uint8_t type, uint256 blockHashKey)
{
    Require(EraseBy<BlockHash>(std::pair(type, blockHashKey)), [=]{ return strprintf("Block hash key %s does not exist", blockHashKey.GetHex()); });
    return Res::Ok();
}

ResVal<uint256> CEvmDvmView::GetBlockHash(uint8_t type, uint256 blockHashKey) const
{
    uint256 blockHash;
    Require(ReadBy<BlockHash>(std::pair(type, blockHashKey), blockHash), [=]{ return strprintf("Block hash key %s does not exist", blockHashKey.GetHex()); });
    return ResVal<uint256>(blockHash, Res::Ok());
}

Res CEvmDvmView::SetTxHash(uint8_t type, uint256 txHashKey, uint256 txHash)
{
    Require(WriteBy<TxHash>(std::pair(type, txHashKey), txHash), [=]{ return strprintf("Failed to store block hash %s to database", txHashKey.GetHex()); });
    return Res::Ok();
}

Res CEvmDvmView::EraseTxHash(uint8_t type, uint256 txHashKey)
{
    Require(EraseBy<TxHash>(std::pair(type, txHashKey)), [=]{ return strprintf("Tx hash key %s does not exist", txHashKey.GetHex()); });
    return Res::Ok();
}

ResVal<uint256> CEvmDvmView::GetTxHash(uint8_t type, uint256 txHashKey) const
{
    uint256 blockHash;
        Require(ReadBy<TxHash>(std::pair(type, txHashKey), blockHash), [=]{ return strprintf("Block hash key %s does not exist", txHashKey.GetHex()); });
    return ResVal<uint256>(blockHash, Res::Ok());
}
