#ifndef DEFI_MASTERNODES_EVM_H
#define DEFI_MASTERNODES_EVM_H

#include <amount.h>
#include <flushablestorage.h>
#include <masternodes/balances.h>
#include <masternodes/res.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

constexpr const uint16_t EVM_TX_SIZE = 32768;

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

class CVMDomainMapView : public virtual CStorageView {
public:
    Res SetVMDomainMapBlockHash(VMDomainEdge type, uint256 blockHashKey, uint256 blockHash);
    ResVal<uint256> GetVMDomainMapBlockHash(VMDomainEdge type, uint256 blockHashKey) const;
    void ForEachVMDomainMapBlockIndexes(std::function<bool(const std::pair<VMDomainEdge, uint256> &, const uint256 &)> callback);

    Res SetVMDomainMapTxHash(VMDomainEdge type, uint256 txHashKey, uint256 txHash);
    ResVal<uint256> GetVMDomainMapTxHash(VMDomainEdge type, uint256 txHashKey) const;
    void ForEachVMDomainMapTxIndexes(std::function<bool(const std::pair<VMDomainEdge, uint256> &, const uint256 &)> callback);

    struct VMDomainBlockHash {
        static constexpr uint8_t prefix() { return 'N'; }
    };

    struct VMDomainTxHash {
        static constexpr uint8_t prefix() { return 'e'; }
    };
};

#endif // DEFI_MASTERNODES_EVM_H
