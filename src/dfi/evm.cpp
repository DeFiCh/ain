#include <ain_rs_exports.h>
#include <dfi/errors.h>
#include <dfi/evm.h>
#include <dfi/res.h>
#include <ffi/ffihelpers.h>
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

CScopedTemplate::CScopedTemplate(BlockTemplateWrapper &evmTemplate)
    : evmTemplate(evmTemplate) {}

std::shared_ptr<CScopedTemplate> CScopedTemplate::Create(const uint64_t dvmBlockNumber,
                                                         const std::string minerAddress,
                                                         const unsigned int difficulty,
                                                         const uint64_t timestamp,
                                                         const std::size_t mnview_ptr) {
    CrossBoundaryResult result;
    BlockTemplateWrapper &evmTemplate =
        evm_try_unsafe_create_template(result, dvmBlockNumber, minerAddress, difficulty, timestamp, mnview_ptr);
    if (result.ok) {
        return std::shared_ptr<CScopedTemplate>(new CScopedTemplate(evmTemplate));
    }
    return nullptr;
}

CScopedTemplate::~CScopedTemplate() {
    XResultStatusLogged(evm_try_unsafe_remove_template(result, evmTemplate));
}

BlockTemplateWrapper &CScopedTemplate::GetTemplate() const {
    return evmTemplate;
}
