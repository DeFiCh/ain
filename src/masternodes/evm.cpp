#include <masternodes/evm.h>
#include <masternodes/errors.h>
#include <masternodes/res.h>
#include <uint256.h>

Res CVMDomainMapView::SetVMDomainMapBlockHash(VMDomainEdge type, uint256 blockHashKey, uint256 blockHash)
{
    return WriteBy<VMDomainBlockHash>(std::pair(static_cast<uint8_t>(type), blockHashKey), blockHash) ? Res::Ok() : DeFiErrors::DatabaseRWFailure(blockHashKey.GetHex());
}

ResVal<uint256> CVMDomainMapView::GetVMDomainMapBlockHash(VMDomainEdge type, uint256 blockHashKey) const
{
    uint256 blockHash;
    if (ReadBy<VMDomainBlockHash>(std::pair(static_cast<uint8_t>(type), blockHashKey), blockHash))
        return ResVal<uint256>(blockHash, Res::Ok());
    return DeFiErrors::DatabaseRWFailure(blockHashKey.GetHex());
}

Res CVMDomainMapView::SetVMDomainMapTxHash(VMDomainEdge type, uint256 txHashKey, uint256 txHash)
{
    return WriteBy<VMDomainTxHash>(std::pair(static_cast<uint8_t>(type), txHashKey), txHash) 
        ? Res::Ok() : DeFiErrors::DatabaseRWFailure(txHashKey.GetHex());
}

ResVal<uint256> CVMDomainMapView::GetVMDomainMapTxHash(VMDomainEdge type, uint256 txHashKey) const
{
    uint256 txHash;
    if (ReadBy<VMDomainTxHash>(std::pair(static_cast<uint8_t>(type), txHashKey), txHash))
        return ResVal<uint256>(txHash, Res::Ok());
    return DeFiErrors::DatabaseRWFailure(txHashKey.GetHex());
}

void CVMDomainMapView::ForEachVMDomainMapBlockIndexes(std::function<bool(const std::pair<VMDomainEdge, uint256> &, const uint256 &)> callback) {
    ForEach<VMDomainBlockHash, std::pair<uint8_t, uint256>, uint256>(
            [&callback](const std::pair<uint8_t, uint256> &key, uint256 val) {
                auto k = std::make_pair(static_cast<VMDomainEdge>(key.first), key.second);
                return callback(k, val);
            });
}

void CVMDomainMapView::ForEachVMDomainMapTxIndexes(std::function<bool(const std::pair<VMDomainEdge, uint256> &, const uint256 &)> callback) {
    ForEach<VMDomainTxHash, std::pair<uint8_t, uint256>, uint256>(
            [&callback](const std::pair<uint8_t, uint256> &key, uint256 val) {
                auto k = std::make_pair(static_cast<VMDomainEdge>(key.first), key.second);
                return callback(k, val);
            });
}