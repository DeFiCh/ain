// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CONSENSUS_GOVERNANCE_H
#define DEFI_MASTERNODES_CONSENSUS_GOVERNANCE_H

#include <masternodes/consensus/txvisitor.h>

struct CGovernanceMessage;
struct CGovernanceHeightMessage;

class CGovernanceConsensus : public CCustomTxVisitor {
    Res storeGovVars(const CGovernanceHeightMessage& obj) const;
public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CGovernanceMessage& obj) const;
    Res operator()(const CGovernanceHeightMessage& obj) const;
};

#endif // DEFI_MASTERNODES_CONSENSUS_GOVERNANCE_H
