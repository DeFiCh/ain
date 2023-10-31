#ifndef DEFI_DFI_EVM_H
#define DEFI_DFI_EVM_H

#include <ain_rs_exports.h>
#include <amount.h>
#include <dfi/consensus/xvm.h>
#include <dfi/res.h>
#include <flushablestorage.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

// EIP-2718 transaction type: legacy - 0x0, EIP2930 - 0x1, EIP1559 - 0x2
enum CEVMTxType {
    LegacyTransaction = 0,
    EIP2930Transaction = 1,
    EIP1559Transaction = 2,
};

using CRawEvmTx = TBytes;

extern std::string CTransferDomainToString(const VMDomain domain);

struct CEvmTxMessage {
    CRawEvmTx evmTx;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(evmTx);
    }
};

class CVMDomainGraphView : public virtual CStorageView {
public:
    Res SetVMDomainBlockEdge(VMDomainEdge type, std::string blockHashKey, std::string blockHash);
    ResVal<std::string> GetVMDomainBlockEdge(VMDomainEdge type, std::string blockHashKey) const;
    void ForEachVMDomainBlockEdges(
        std::function<bool(const std::pair<VMDomainEdge, std::string> &, const std::string &)> callback,
        const std::pair<VMDomainEdge, std::string> &start = {});

    Res SetVMDomainTxEdge(VMDomainEdge type, std::string txHashKey, std::string txHash);
    ResVal<std::string> GetVMDomainTxEdge(VMDomainEdge type, std::string txHashKey) const;
    void ForEachVMDomainTxEdges(
        std::function<bool(const std::pair<VMDomainEdge, std::string> &, const std::string &)> callback,
        const std::pair<VMDomainEdge, std::string> &start = {});

    struct VMDomainBlockEdge {
        static constexpr uint8_t prefix() { return 'N'; }
    };

    struct VMDomainTxEdge {
        static constexpr uint8_t prefix() { return 'e'; }
    };
};

class CScopedTemplate {
    explicit CScopedTemplate(BlockTemplateWrapper &blockTemplate);

    BlockTemplateWrapper &evmTemplate;

public:
    static std::shared_ptr<CScopedTemplate> Create(const uint64_t dvmBlockNumber,
                                                   std::string minerAddress,
                                                   unsigned int difficulty,
                                                   const uint64_t timestamp,
                                                   const std::size_t mnview_ptr);
    ~CScopedTemplate();

    BlockTemplateWrapper &GetTemplate() const;
};

#endif  // DEFI_DFI_EVM_H
