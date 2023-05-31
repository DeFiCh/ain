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

extern std::string CTransferDomainTypeToString(const CTransferDomainType type);

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
    uint256 GetBlockHash(uint8_t, uint256) const;
    void SetBlockHash(uint8_t, uint256, uint256);

    struct BlockHash {
        static constexpr uint8_t prefix() { return 'N'; }
    };
};

#endif // DEFI_MASTERNODES_EVM_H
