// Copyright (c) 2022 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_CONSENSUS_TXVISITOR_H
#define DEFI_MASTERNODES_CONSENSUS_TXVISITOR_H

#include <amount.h>
#include <masternodes/balances.h>
#include <masternodes/oracles.h>
#include <masternodes/res.h>
#include <masternodes/vault.h>

struct CBalances;
class CCoinsViewCache;
class CCollateralLoans;
struct CCreateProposalMessage;
class CCustomCSView;
struct CLoanSchemeData;
class CScript;
class CTokenImplementation;
class CTransaction;

namespace Consensus {
struct Params;
}

class CCustomTxVisitor {
protected:
    uint32_t height;
    CCustomCSView &mnview;
    const CTransaction &tx;
    const CCoinsViewCache &coins;
    const Consensus::Params &consensus;
    const uint64_t time;
    const uint32_t txn;

public:
    CCustomTxVisitor(const CTransaction &tx,
                     uint32_t height,
                     const CCoinsViewCache &coins,
                     CCustomCSView &mnview,
                     const Consensus::Params &consensus,
                     const uint64_t time,
                     const uint32_t txn);

protected:
    Res HasAuth(const CScript &auth) const;
    Res HasCollateralAuth(const uint256 &collateralTx) const;
    Res HasFoundationAuth() const;

    Res CheckCustomTx() const;
    Res TransferTokenBalance(DCT_ID id, CAmount amount, const CScript &from, const CScript &to) const;
    ResVal<CBalances> MintedTokens(uint32_t mintingOutputsStart) const;
    Res SetShares(const CScript &owner, const TAmounts &balances) const;
    Res DelShares(const CScript &owner, const TAmounts &balances) const;
    void CalculateOwnerRewards(const CScript &owner) const;
    Res SubBalanceDelShares(const CScript &owner, const CBalances &balance) const;
    Res AddBalanceSetShares(const CScript &owner, const CBalances &balance) const;
    Res AddBalancesSetShares(const CAccounts &accounts) const;
    Res SubBalancesDelShares(const CAccounts &accounts) const;
    Res CollateralPctCheck(const bool hasDUSDLoans, const CCollateralLoans &collateralsLoans, const uint32_t ratio) const;
    ResVal<CCollateralLoans> CheckCollateralRatio(const CVaultId& vaultId, const CLoanSchemeData& scheme, const CBalances& collaterals, bool useNextPrice, bool requireLivePrice) const;
    Res CheckNextCollateralRatio(const CVaultId& vaultId, const CLoanSchemeData& scheme, const CBalances& collaterals, const bool hasDUSDLoans) const;
};

#endif // DEFI_MASTERNODES_CONSENSUS_TXVISITOR_H

