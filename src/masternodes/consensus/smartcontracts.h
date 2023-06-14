// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CONSENSUS_SMARTCONTRACTS_H
#define DEFI_MASTERNODES_CONSENSUS_SMARTCONTRACTS_H

#include <masternodes/consensus/txvisitor.h>

struct CSmartContractMessage;
struct CFutureSwapMessage;

class CSmartContractsConsensus : public CCustomTxVisitor {
    Res HandleDFIP2201Contract(const CSmartContractMessage& obj) const;
public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CSmartContractMessage& obj) const;
    Res operator()(const CFutureSwapMessage& obj) const;
};

#endif // DEFI_MASTERNODES_CONSENSUS_SMARTCONTRACTS_H
