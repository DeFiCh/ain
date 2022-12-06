// Copyright (c) 2022 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CONSENSUS_POOLPAIRS_H
#define DEFI_MASTERNODES_CONSENSUS_POOLPAIRS_H

#include <masternodes/consensus/txvisitor.h>

struct CCreatePoolPairMessage;
struct CUpdatePoolPairMessage;
struct CPoolSwapMessage;
struct CPoolSwapMessageV2;
struct CLiquidityMessage;
struct CRemoveLiquidityMessage;

class CPoolPairsConsensus : public CCustomTxVisitor {
    Res EraseEmptyBalances(TAmounts &balances) const;
public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CCreatePoolPairMessage& obj) const;
    Res operator()(const CUpdatePoolPairMessage& obj) const;
    Res operator()(const CPoolSwapMessage& obj) const;
    Res operator()(const CPoolSwapMessageV2& obj) const;
    Res operator()(const CLiquidityMessage& obj) const;
    Res operator()(const CRemoveLiquidityMessage& obj) const;
};

#endif // DEFI_MASTERNODES_CONSENSUS_POOLPAIRS_H
