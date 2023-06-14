// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CONSENSUS_LOANS_H
#define DEFI_MASTERNODES_CONSENSUS_LOANS_H

#include <masternodes/consensus/txvisitor.h>

struct CLoanSetCollateralTokenMessage;
struct CLoanSetLoanTokenMessage;
struct CLoanUpdateLoanTokenMessage;
struct CLoanSchemeMessage;
struct CDefaultLoanSchemeMessage;
struct CDestroyLoanSchemeMessage;
class CLoanTakeLoanMessage;
class CLoanPaybackLoanMessage;
class CLoanPaybackLoanV2Message;
class CPaybackWithCollateralMessage;

class CLoansConsensus : public CCustomTxVisitor {
    [[nodiscard]] bool IsPaybackWithCollateral(const std::map<DCT_ID, CBalances> &loans) const;
    [[nodiscard]] bool IsTokensMigratedToGovVar() const;
    [[nodiscard]] Res PaybackWithCollateral(const CVaultData &vault, const CVaultId &vaultId, uint32_t height, uint64_t time) const;
public:
    using CCustomTxVisitor::CCustomTxVisitor;
    Res operator()(const CLoanSetCollateralTokenMessage& obj) const;
    Res operator()(const CLoanSetLoanTokenMessage& obj) const;
    Res operator()(const CLoanUpdateLoanTokenMessage& obj) const;
    Res operator()(const CLoanSchemeMessage& obj) const;
    Res operator()(const CDefaultLoanSchemeMessage& obj) const;
    Res operator()(const CDestroyLoanSchemeMessage& obj) const;
    Res operator()(const CLoanTakeLoanMessage& obj) const;
    Res operator()(const CLoanPaybackLoanMessage& obj) const;
    Res operator()(const CLoanPaybackLoanV2Message& obj) const;
    Res operator()(const CPaybackWithCollateralMessage& obj) const;
};

#endif // DEFI_MASTERNODES_CONSENSUS_LOANS_H
