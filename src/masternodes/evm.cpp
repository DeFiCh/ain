#include <masternodes/evm.h>
#include <masternodes/errors.h>
#include <masternodes/res.h>
#include <uint256.h>

Res CVMDomainGraphView::SetVMDomainBlockEdge(VMDomainEdge type, uint256 blockHashKey, uint256 blockHash)
{
    return WriteBy<VMDomainBlockEdge>(std::pair(static_cast<uint8_t>(type), blockHashKey), blockHash)
            ? Res::Ok() : DeFiErrors::DatabaseRWFailure(blockHashKey.GetHex());
}

ResVal<uint256> CVMDomainGraphView::GetVMDomainBlockEdge(VMDomainEdge type, uint256 blockHashKey) const
{
    uint256 blockHash;
    if (ReadBy<VMDomainBlockEdge>(std::pair(static_cast<uint8_t>(type), blockHashKey), blockHash))
        return ResVal<uint256>(blockHash, Res::Ok());
    return DeFiErrors::DatabaseKeyNotFound(blockHashKey.GetHex());
}

Res CVMDomainGraphView::SetVMDomainTxEdge(VMDomainEdge type, uint256 txHashKey, uint256 txHash)
{
    return WriteBy<VMDomainTxEdge>(std::pair(static_cast<uint8_t>(type), txHashKey), txHash) 
        ? Res::Ok() : DeFiErrors::DatabaseRWFailure(txHashKey.GetHex());
}

ResVal<uint256> CVMDomainGraphView::GetVMDomainTxEdge(VMDomainEdge type, uint256 txHashKey) const
{
    uint256 txHash;
    if (ReadBy<VMDomainTxEdge>(std::pair(static_cast<uint8_t>(type), txHashKey), txHash))
        return ResVal<uint256>(txHash, Res::Ok());
    return DeFiErrors::DatabaseKeyNotFound(txHashKey.GetHex());
}

void CVMDomainGraphView::ForEachVMDomainBlockEdges(std::function<bool(const std::pair<VMDomainEdge, uint256> &, const uint256 &)> callback) {
    ForEach<VMDomainBlockEdge, std::pair<uint8_t, uint256>, uint256>(
            [&callback](const std::pair<uint8_t, uint256> &key, uint256 val) {
                auto k = std::make_pair(static_cast<VMDomainEdge>(key.first), key.second);
                return callback(k, val);
            });
}

void CVMDomainGraphView::ForEachVMDomainTxEdges(std::function<bool(const std::pair<VMDomainEdge, uint256> &, const uint256 &)> callback) {
    ForEach<VMDomainTxEdge, std::pair<uint8_t, uint256>, uint256>(
            [&callback](const std::pair<uint8_t, uint256> &key, uint256 val) {
                auto k = std::make_pair(static_cast<VMDomainEdge>(key.first), key.second);
                return callback(k, val);
            });
}