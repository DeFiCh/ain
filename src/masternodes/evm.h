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

enum CEvmDvmMapType : uint8_t {
    DvmEvm            = 0x01,
    EvmDvm            = 0x02,
};

class CEvmDvmView : public virtual CStorageView {
public:
    Res SetBlockHash(uint8_t type, uint256 blockHashKey, uint256 blockHash);
    ResVal<uint256> GetBlockHash(uint8_t type, uint256 blockHashKey) const;
    void ForEachBlockIndexes(std::function<bool(const std::pair<uint8_t, uint256> &, const uint256 &)> callback);

    Res SetTxHash(uint8_t type, uint256 txHashKey, uint256 txHash);
    ResVal<uint256> GetTxHash(uint8_t type, uint256 txHashKey) const;
    void ForEachTxIndexes(std::function<bool(const std::pair<uint8_t, uint256> &, const uint256 &)> callback);

    struct BlockHash {
        static constexpr uint8_t prefix() { return 'N'; }
    };

    struct TxHash {
        static constexpr uint8_t prefix() { return 'e'; }
    };
};

#endif // DEFI_MASTERNODES_EVM_H
