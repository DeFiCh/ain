#include <ain_rs_exports.h>
#include <masternodes/evm.h>
#include <masternodes/errors.h>
#include <masternodes/res.h>
#include <uint256.h>

Res CVMDomainGraphView::SetVMDomainBlockEdge(VMDomainEdge type, std::string blockHashKey, std::string blockHash)
{
    return WriteBy<VMDomainBlockEdge>(std::pair(static_cast<uint8_t>(type), blockHashKey), blockHash)
        ? Res::Ok() : DeFiErrors::DatabaseRWFailure(blockHashKey);
}

ResVal<std::string> CVMDomainGraphView::GetVMDomainBlockEdge(VMDomainEdge type, std::string blockHashKey) const
{
    std::string blockHash;
    if (ReadBy<VMDomainBlockEdge>(std::pair(static_cast<uint8_t>(type), blockHashKey), blockHash))
        return ResVal<std::string>(blockHash, Res::Ok());
    return DeFiErrors::DatabaseKeyNotFound(blockHashKey);
}

Res CVMDomainGraphView::SetVMDomainTxEdge(VMDomainEdge type, std::string txHashKey, std::string txHash)
{
    return WriteBy<VMDomainTxEdge>(std::pair(static_cast<uint8_t>(type), txHashKey), txHash) 
        ? Res::Ok() : DeFiErrors::DatabaseRWFailure(txHashKey);
}

ResVal<std::string> CVMDomainGraphView::GetVMDomainTxEdge(VMDomainEdge type, std::string txHashKey) const
{
    std::string txHash;
    if (ReadBy<VMDomainTxEdge>(std::pair(static_cast<uint8_t>(type), txHashKey), txHash))
        return ResVal<std::string>(txHash, Res::Ok());
    return DeFiErrors::DatabaseKeyNotFound(txHashKey);
}

void CVMDomainGraphView::ForEachVMDomainBlockEdges(std::function<bool(const std::pair<VMDomainEdge, std::string> &, const std::string &)> callback, const std::pair<VMDomainEdge, std::string> &start) {
    ForEach<VMDomainBlockEdge, std::pair<uint8_t, std::string>, std::string>(
            [&callback](const std::pair<uint8_t, std::string> &key, std::string val) {
                auto k = std::make_pair(static_cast<VMDomainEdge>(key.first), key.second);
                return callback(k, val);
            }, std::make_pair(static_cast<uint8_t>(start.first), start.second));
}

void CVMDomainGraphView::ForEachVMDomainTxEdges(std::function<bool(const std::pair<VMDomainEdge, std::string> &, const std::string &)> callback, const std::pair<VMDomainEdge, std::string> &start) {
    ForEach<VMDomainTxEdge, std::pair<uint8_t, std::string>, std::string>(
            [&callback](const std::pair<uint8_t, std::string> &key, std::string val) {
                auto k = std::make_pair(static_cast<VMDomainEdge>(key.first), key.second);
                return callback(k, val);
            }, std::make_pair(static_cast<uint8_t>(start.first), start.second));
}

CScopedQueueID::CScopedQueueID() : evmQueueId{}, isValid(false) {}

CScopedQueueID::CScopedQueueID(const uint64_t timestamp) : isValid(false) {
    CrossBoundaryResult result;
    uint64_t queueId = evm_try_unsafe_create_queue(result, timestamp);
    if (result.ok) {
        evmQueueId = std::make_shared<uint64_t>(queueId);
        isValid = true;
    }
}

CScopedQueueID::~CScopedQueueID() {
    if (isValid && evmQueueId.use_count() == 1) {
        CrossBoundaryResult result;
        evm_try_unsafe_remove_queue(result, *evmQueueId);
        if (!result.ok) {
            LogPrintf("Failed to destroy queue %d\n", *evmQueueId);
        }
    }
}

CScopedQueueID::operator bool() const {
    return isValid;
}

uint64_t CScopedQueueID::operator*() const {
    if (!isValid) {
        throw std::runtime_error("evmQueueId is not valid");
    }
    return *evmQueueId;
}
