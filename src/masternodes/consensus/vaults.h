// Copyright (c) 2022 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CONSENSUS_VAULTS_H
#define DEFI_MASTERNODES_CONSENSUS_VAULTS_H

#include <masternodes/consensus/txvisitor.h>

struct CVaultMessage;
struct CCloseVaultMessage;
struct CUpdateVaultMessage;
struct CDepositToVaultMessage;
struct CWithdrawFromVaultMessage;
struct CAuctionBidMessage;

class CVaultsConsensus : public CCustomTxVisitor {
public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CVaultMessage& obj) const;
    Res operator()(const CCloseVaultMessage& obj) const;
    Res operator()(const CUpdateVaultMessage& obj) const;
    Res operator()(const CDepositToVaultMessage& obj) const;
    Res operator()(const CWithdrawFromVaultMessage& obj) const;
    Res operator()(const CAuctionBidMessage& obj) const;
};

#endif // DEFI_MASTERNODES_CONSENSUS_VAULTS_H
