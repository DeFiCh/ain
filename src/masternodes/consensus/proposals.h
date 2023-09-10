// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CONSENSUS_PROPOSALS_H
#define DEFI_MASTERNODES_CONSENSUS_PROPOSALS_H

#include <masternodes/consensus/txvisitor.h>

struct CCreateProposalMessage;
struct CProposalVoteMessage;

class CProposalsConsensus : public CCustomTxVisitor {
    [[nodiscard]] Res CheckProposalTx(const CCreateProposalMessage &msg) const;
    [[nodiscard]] Res IsOnChainGovernanceEnabled() const;
public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CCreateProposalMessage& obj) const;
    Res operator()(const CProposalVoteMessage& obj) const;
};

#endif // DEFI_MASTERNODES_CONSENSUS_PROPOSALS_H
