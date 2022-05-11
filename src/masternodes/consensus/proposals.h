// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CONSENSUS_PROPOSALS_H
#define DEFI_MASTERNODES_CONSENSUS_PROPOSALS_H

#include <masternodes/consensus/txvisitor.h>

struct CCreatePropMessage;
struct CPropVoteMessage;

class CProposalsConsensus : public CCustomTxVisitor {
public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CCreatePropMessage& obj) const;
    Res operator()(const CPropVoteMessage& obj) const;
};

#endif // DEFI_MASTERNODES_CONSENSUS_PROPOSALS_H
