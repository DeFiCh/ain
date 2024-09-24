// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_DFI_CONSENSUS_GOVERNANCE_H
#define DEFI_DFI_CONSENSUS_GOVERNANCE_H

#include <dfi/consensus/txvisitor.h>

struct CGovernanceMessage;
struct CGovernanceHeightMessage;
struct CGovernanceUnsetMessage;
struct CGovernanceUnsetHeightMessage;
struct CGovernanceClearHeightMessage;

class CGovernanceConsensus : public CCustomTxVisitor {
public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CGovernanceMessage &obj) const;
    Res operator()(const CGovernanceHeightMessage &obj) const;
    Res operator()(const CGovernanceUnsetMessage &obj) const;
    Res operator()(const CGovernanceUnsetHeightMessage &obj) const;
    Res operator()(const CGovernanceClearHeightMessage &obj) const;
};

#endif  // DEFI_DFI_CONSENSUS_GOVERNANCE_H
