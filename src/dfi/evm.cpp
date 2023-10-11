#include <ain_rs_exports.h>
#include <dfi/errors.h>
#include <dfi/evm.h>
#include <dfi/res.h>
#include <uint256.h>

Res CVMDomainGraphView::SetVMDomainBlockEdge(VMDomainEdge type, std::string blockHashKey, std::string blockHash) {
    return WriteBy<VMDomainBlockEdge>(std::pair(static_cast<uint8_t>(type), blockHashKey), blockHash)
               ? Res::Ok()
               : DeFiErrors::DatabaseRWFailure(blockHashKey);
}

ResVal<std::string> CVMDomainGraphView::GetVMDomainBlockEdge(VMDomainEdge type, std::string blockHashKey) const {
    std::string blockHash;
    if (ReadBy<VMDomainBlockEdge>(std::pair(static_cast<uint8_t>(type), blockHashKey), blockHash)) {
        return ResVal<std::string>(blockHash, Res::Ok());
    }
    return DeFiErrors::DatabaseKeyNotFound(blockHashKey);
}

Res CVMDomainGraphView::SetVMDomainTxEdge(VMDomainEdge type, std::string txHashKey, std::string txHash) {
    return WriteBy<VMDomainTxEdge>(std::pair(static_cast<uint8_t>(type), txHashKey), txHash)
               ? Res::Ok()
               : DeFiErrors::DatabaseRWFailure(txHashKey);
}

ResVal<std::string> CVMDomainGraphView::GetVMDomainTxEdge(VMDomainEdge type, std::string txHashKey) const {
    std::string txHash;
    if (ReadBy<VMDomainTxEdge>(std::pair(static_cast<uint8_t>(type), txHashKey), txHash)) {
        return ResVal<std::string>(txHash, Res::Ok());
    }
    return DeFiErrors::DatabaseKeyNotFound(txHashKey);
}

void CVMDomainGraphView::ForEachVMDomainBlockEdges(
    std::function<bool(const std::pair<VMDomainEdge, std::string> &, const std::string &)> callback,
    const std::pair<VMDomainEdge, std::string> &start) {
    ForEach<VMDomainBlockEdge, std::pair<uint8_t, std::string>, std::string>(
        [&callback](const std::pair<uint8_t, std::string> &key, std::string val) {
            auto k = std::make_pair(static_cast<VMDomainEdge>(key.first), key.second);
            return callback(k, val);
        },
        std::make_pair(static_cast<uint8_t>(start.first), start.second));
}

void CVMDomainGraphView::ForEachVMDomainTxEdges(
    std::function<bool(const std::pair<VMDomainEdge, std::string> &, const std::string &)> callback,
    const std::pair<VMDomainEdge, std::string> &start) {
    ForEach<VMDomainTxEdge, std::pair<uint8_t, std::string>, std::string>(
        [&callback](const std::pair<uint8_t, std::string> &key, std::string val) {
            auto k = std::make_pair(static_cast<VMDomainEdge>(key.first), key.second);
            return callback(k, val);
        },
        std::make_pair(static_cast<uint8_t>(start.first), start.second));
}

CScopedTemplateID::CScopedTemplateID(uint64_t id)
    : evmTemplateId(id) {}

std::shared_ptr<CScopedTemplateID> CScopedTemplateID::Create(const uint64_t dvmBlockNumber, std::string minerAddress, const uint64_t timestamp) {
    CrossBoundaryResult result;
    uint64_t templateId = evm_try_unsafe_create_template(result, dvmBlockNumber, minerAddress, timestamp);
    if (result.ok) {
        return std::shared_ptr<CScopedTemplateID>(new CScopedTemplateID(templateId));
    }
    return nullptr;
}

CScopedTemplateID::~CScopedTemplateID() {
    CrossBoundaryResult result;
    evm_try_unsafe_remove_template(result, evmTemplateId);
    if (!result.ok) {
        LogPrintf("Failed to destroy queue %d\n", evmTemplateId);
    }
}

uint64_t CScopedTemplateID::GetTemplateID() const {
    return evmTemplateId;
}
