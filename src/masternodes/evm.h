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

struct CEVMTransaction {
    uint256 hash;
    std::string sender;
    uint64_t nonce;
    uint64_t gasPrice;
    uint64_t gasLimit;
    bool createTx;
    std::string to;
    uint64_t value;
    std::vector<uint8_t> data;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(hash);
        READWRITE(sender);
        READWRITE(nonce);
        READWRITE(gasPrice);
        READWRITE(gasLimit);
        READWRITE(createTx);
        READWRITE(to);
        READWRITE(value);
        READWRITE(data);
    }
};

class CVMDomainGraphView : public virtual CStorageView {
public:
    Res SetVMDomainBlockEdge(VMDomainEdge type, uint256 blockHashKey, uint256 blockHash);
    ResVal<uint256> GetVMDomainBlockEdge(VMDomainEdge type, uint256 blockHashKey) const;
    void ForEachVMDomainBlockEdges(std::function<bool(const std::pair<VMDomainEdge, uint256> &, const uint256 &)> callback, const std::pair<VMDomainEdge, uint256> &start = {});

    Res SetVMDomainTxEdge(VMDomainEdge type, uint256 txHashKey, uint256 txHash);
    ResVal<uint256> GetVMDomainTxEdge(VMDomainEdge type, uint256 txHashKey) const;
    void ForEachVMDomainTxEdges(std::function<bool(const std::pair<VMDomainEdge, uint256> &, const uint256 &)> callback, const std::pair<VMDomainEdge, uint256> &start = {});

    Res SetEVMTransaction(uint256 txHashKey, CEVMTransaction evmTx);
    ResVal<CEVMTransaction> GetEVMTransaction(uint256 txHashKey);

    struct VMDomainBlockEdge {
        static constexpr uint8_t prefix() { return 'N'; }
    };

    struct VMDomainTxEdge {
        static constexpr uint8_t prefix() { return 'e'; }
    };

    struct EVMTransaction {
        static constexpr uint8_t prefix() { return 'h'; }
    };
};

#endif // DEFI_MASTERNODES_EVM_H
