#include <masternodes/evm.h>
#include <masternodes/errors.h>
#include <masternodes/res.h>
#include <uint256.h>

Res CEvmDvmView::SetBlockHash(uint8_t type, uint256 blockHashKey, uint256 blockHash)
{
    return WriteBy<BlockHash>(std::pair(type, blockHashKey), blockHash) ? Res::Ok() : DeFiErrors::StoreBlockFailed(blockHashKey.GetHex());
}

Res CEvmDvmView::EraseBlockHash(uint8_t type, uint256 blockHashKey)
{
    return EraseBy<BlockHash>(std::pair(type, blockHashKey)) ? Res::Ok() : DeFiErrors::FetchBlockFailed(blockHashKey.GetHex());
}

ResVal<uint256> CEvmDvmView::GetBlockHash(uint8_t type, uint256 blockHashKey) const
{
    uint256 blockHash;
    if (ReadBy<BlockHash>(std::pair(type, blockHashKey), blockHash))
        return ResVal<uint256>(blockHash, Res::Ok());
    return DeFiErrors::FetchBlockFailed(blockHashKey.GetHex());
}

Res CEvmDvmView::SetTxHash(uint8_t type, uint256 txHashKey, uint256 txHash)
{
    return WriteBy<TxHash>(std::pair(type, txHashKey), txHash) ? Res::Ok() : DeFiErrors::StoreTxFailed(txHashKey.GetHex());
}

Res CEvmDvmView::EraseTxHash(uint8_t type, uint256 txHashKey)
{
    return EraseBy<TxHash>(std::pair(type, txHashKey)) ? Res::Ok() : DeFiErrors::FetchTxFailed(txHashKey.GetHex());
}

ResVal<uint256> CEvmDvmView::GetTxHash(uint8_t type, uint256 txHashKey) const
{
    uint256 txHash;
    if (ReadBy<TxHash>(std::pair(type, txHashKey), txHash))
        return ResVal<uint256>(txHash, Res::Ok());
    return DeFiErrors::FetchTxFailed(txHashKey.GetHex());
}

void CEvmDvmView::ForEachBlockIndexes(std::function<bool(const std::pair<uint8_t, uint256> &, const uint256 &)> callback) {
    ForEach<BlockHash, std::pair<uint8_t, uint256>, uint256>(
            [&callback](const std::pair<uint8_t, uint256> &key, uint256 val) {
                return callback(key, val);
            });
}

void CEvmDvmView::ForEachTxIndexes(std::function<bool(const std::pair<uint8_t, uint256> &, const uint256 &)> callback) {
    ForEach<TxHash, std::pair<uint8_t, uint256>, uint256>(
            [&callback](const std::pair<uint8_t, uint256> &key, uint256 val) {
                return callback(key, val);
            });
}