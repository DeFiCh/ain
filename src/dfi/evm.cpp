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

CScopedTemplateID::CScopedTemplateID(BlockTemplate* blockTemplate, BackendLock* lock)
    : blockTemplate(blockTemplate), lock(lock) {}

std::shared_ptr<CScopedTemplateID> CScopedTemplateID::Create(const uint64_t dvmBlockNumber,
                                                             const std::string minerAddress,
                                                             const unsigned int difficulty,
                                                             const uint64_t timestamp) {

    LogPrintf("Creating a new CScopedTemplateID id\n");
    BackendLock* lock = get_backend_lock();

    CrossBoundaryResult result;
    BlockTemplate * blockTemplate = evm_try_unsafe_create_block_template(result, *lock, dvmBlockNumber, minerAddress, difficulty, timestamp);
    if (result.ok) {
        return std::shared_ptr<CScopedTemplateID>(new CScopedTemplateID(blockTemplate, lock));
    }
    return nullptr;
}

CScopedTemplateID::~CScopedTemplateID() {
    LogPrintf("Removing block template");
    CrossBoundaryResult result;
    // evm_try_unsafe_remove_block_template(result, *blockTemplate);

    // LogPrintf("Result : result.ok %d, result reason :%s\n", result.ok, result.reason.c_str());
    // if (!result.ok) {
    //     LogPrintf("Failed to destroy queue\n");
    // }
    free_backend_lock(lock);
}

BlockTemplate* CScopedTemplateID::GetTemplateID() const {
    return blockTemplate;
}
