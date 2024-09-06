// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_DFI_CONSENSUS_TOKENLOCK_H
#define DEFI_DFI_CONSENSUS_TOKENLOCK_H

#include <dfi/consensus/txvisitor.h>

struct CReleaseLockMessage;

class CTokenLockConsensus : public CCustomTxVisitor {
public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CReleaseLockMessage &obj) const;
};

#endif  // DEFI_DFI_CONSENSUS_TOKENLOCK_H
