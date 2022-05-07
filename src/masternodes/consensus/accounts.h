// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CONSENSUS_ACCOUNTS_H
#define DEFI_MASTERNODES_CONSENSUS_ACCOUNTS_H

#include <masternodes/consensus/txvisitor.h>

struct CUtxosToAccountMessage;
struct CAccountToUtxosMessage;
struct CAccountToAccountMessage;
struct CAnyAccountsToAccountsMessage;

class CAccountsConsensus : public CCustomTxVisitor {
public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CUtxosToAccountMessage& obj) const;
    Res operator()(const CAccountToUtxosMessage& obj) const;
    Res operator()(const CAccountToAccountMessage& obj) const;
    Res operator()(const CAnyAccountsToAccountsMessage& obj) const;
};

#endif // DEFI_MASTERNODES_CONSENSUS_ACCOUNTS_H
