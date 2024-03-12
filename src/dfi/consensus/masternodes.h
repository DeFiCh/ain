// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_DFI_CONSENSUS_MASTERNODES_H
#define DEFI_DFI_CONSENSUS_MASTERNODES_H

#include <dfi/consensus/txvisitor.h>

struct CCreateMasterNodeMessage;
struct CResignMasterNodeMessage;
struct CUpdateMasterNodeMessage;

class CMasternodesConsensus : public CCustomTxVisitor {
    [[nodiscard]] Res CheckMasternodeCreationTx() const;

public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CCreateMasterNodeMessage &obj) const;
    Res operator()(const CResignMasterNodeMessage &obj) const;
    Res operator()(const CUpdateMasterNodeMessage &obj) const;
};

#endif  // DEFI_DFI_CONSENSUS_MASTERNODES_H
