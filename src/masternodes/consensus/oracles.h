// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CONSENSUS_ORACLES_H
#define DEFI_MASTERNODES_CONSENSUS_ORACLES_H

#include <masternodes/consensus/txvisitor.h>

struct CAppointOracleMessage;
struct CUpdateOracleAppointMessage;
struct CRemoveOracleAppointMessage;
struct CSetOracleDataMessage;

class COraclesConsensus : public CCustomTxVisitor {
    [[nodiscard]] Res NormalizeTokenCurrencyPair(std::set<CTokenCurrencyPair> &tokenCurrency) const;
public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CAppointOracleMessage& obj) const;
    Res operator()(const CUpdateOracleAppointMessage& obj) const;
    Res operator()(const CRemoveOracleAppointMessage& obj) const;
    Res operator()(const CSetOracleDataMessage& obj) const;
};

#endif // DEFI_MASTERNODES_CONSENSUS_ORACLES_H
