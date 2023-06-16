#include <masternodes/evm.h>
#include <masternodes/errors.h>
#include <masternodes/res.h>
#include <uint256.h>

Res CVMDomainMapView::SetVMDomainMapBlockHash(uint8_t type, uint256 blockHashKey, uint256 blockHash)
{
    return WriteBy<VMDomainBlockHash>(std::pair(type, blockHashKey), blockHash) ? Res::Ok() : DeFiErrors::DatabaseRWFailure(blockHashKey.GetHex());
}

ResVal<uint256> CVMDomainMapView::GetVMDomainMapBlockHash(uint8_t type, uint256 blockHashKey) const
{
    uint256 blockHash;
    if (ReadBy<VMDomainBlockHash>(std::pair(type, blockHashKey), blockHash))
        return ResVal<uint256>(blockHash, Res::Ok());
    return DeFiErrors::DatabaseRWFailure(blockHashKey.GetHex());
}

Res CVMDomainMapView::SetVMDomainMapTxHash(uint8_t type, uint256 txHashKey, uint256 txHash)
{
    return WriteBy<VMDomainTxHash>(std::pair(type, txHashKey), txHash) ? Res::Ok() : DeFiErrors::DatabaseRWFailure(txHashKey.GetHex());
}

ResVal<uint256> CVMDomainMapView::GetVMDomainMapTxHash(uint8_t type, uint256 txHashKey) const
{
    uint256 txHash;
    if (ReadBy<VMDomainTxHash>(std::pair(type, txHashKey), txHash))
        return ResVal<uint256>(txHash, Res::Ok());
    return DeFiErrors::DatabaseRWFailure(txHashKey.GetHex());
}

void CVMDomainMapView::ForEachVMDomainMapBlockIndexes(std::function<bool(const std::pair<uint8_t, uint256> &, const uint256 &)> callback) {
    ForEach<VMDomainBlockHash, std::pair<uint8_t, uint256>, uint256>(
            [&callback](const std::pair<uint8_t, uint256> &key, uint256 val) {
                return callback(key, val);
            });
}

void CVMDomainMapView::ForEachVMDomainMapTxIndexes(std::function<bool(const std::pair<uint8_t, uint256> &, const uint256 &)> callback) {
    ForEach<VMDomainTxHash, std::pair<uint8_t, uint256>, uint256>(
            [&callback](const std::pair<uint8_t, uint256> &key, uint256 val) {
                return callback(key, val);
            });
}