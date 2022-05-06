// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CONSENSUS_MASTERNODES_H
#define DEFI_MASTERNODES_CONSENSUS_MASTERNODES_H

#include <masternodes/consensus/txvisitor.h>

struct CCreateMasterNodeMessage;
struct CResignMasterNodeMessage;
struct CUpdateMasterNodeMessage;

class CMasternodesConsensus : public CCustomTxVisitor {
public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CCreateMasterNodeMessage& obj) const;
    Res operator()(const CResignMasterNodeMessage& obj) const;
    Res operator()(const CUpdateMasterNodeMessage& obj) const;
};

#endif // DEFI_MASTERNODES_CONSENSUS_MASTERNODES_H
