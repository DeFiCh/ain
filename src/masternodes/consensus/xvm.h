// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CONSENSUS_XVM_H
#define DEFI_MASTERNODES_CONSENSUS_XVM_H

#include <masternodes/consensus/txvisitor.h>

struct CEvmTxMessage;
struct CTransferDomainMessage;

enum class VMDomain : uint8_t {
    NONE     = 0x00,
    // UTXO Reserved
    UTXO     = 0x01,
    DVM      = 0x02,
    EVM      = 0x03,
};

class CXVMConsensus : public CCustomTxVisitor {
    [[nodiscard]] Res ValidateTransferDomain(const CTransferDomainMessage& obj) const;
public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CEvmTxMessage& obj) const;
    Res operator()(const CTransferDomainMessage& obj) const;
};

#endif // DEFI_MASTERNODES_CONSENSUS_XVM_H
