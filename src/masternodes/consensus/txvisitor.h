// Copyright (c) 2021 The DeFi Blockchain Developers
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
class CCustomCSView;
class CFutureSwapView;
struct CLoanSchemeData;
class CScript;
class CTokenImplementation;
class CTransaction;

namespace Consensus {
struct Params;
}

class CCustomTxVisitor
{
protected:
    uint32_t txn;
    uint64_t time;
    uint32_t height;
    CCustomCSView& mnview;
    CFutureSwapView& futureSwapView;
    const CTransaction& tx;
    const CCoinsViewCache& coins;
    const Consensus::Params& consensus;

public:
    CCustomTxVisitor(CCustomCSView& mnview, CFutureSwapView& futureSwapView, const CCoinsViewCache& coins, const CTransaction& tx, const Consensus::Params& consensus, uint32_t height, uint64_t time, uint32_t txn);

protected:
    Res CheckCustomTx() const;
    Res HasFoundationAuth() const;
    Res CheckTokenCreationTx() const;
    Res CheckMasternodeCreationTx() const;
    Res CheckProposalTx(uint8_t type) const;
    ResVal<CBalances> BurntTokens() const;
    Res HasAuth(const CScript& auth) const;
    CAmount CalculateTakerFee(CAmount amount) const;
    Res EraseEmptyBalances(TAmounts& balances) const;
    void CalculateOwnerRewards(const CScript& owner) const;
    Res HasCollateralAuth(const uint256& collateralTx) const;
    ResVal<CBalances> MintedTokens(uint32_t mintingOutputsStart) const;
    Res SetShares(const CScript& owner, const TAmounts& balances) const;
    Res DelShares(const CScript& owner, const TAmounts& balances) const;
    Res SubBalanceDelShares(const CScript& owner, const CBalances& balance) const;
    Res AddBalanceSetShares(const CScript& owner, const CBalances& balance) const;
    Res AddBalancesSetShares(const CAccounts& accounts) const;
    Res SubBalancesDelShares(const CAccounts& accounts) const;
    Res NormalizeTokenCurrencyPair(std::set<CTokenCurrencyPair>& tokenCurrency) const;
    ResVal<CScript> MintableToken(DCT_ID id, const CTokenImplementation& token) const;
    Res TransferTokenBalance(DCT_ID id, CAmount amount, const CScript& from, const CScript& to) const;
    ResVal<CCollateralLoans> CheckCollateralRatio(const CVaultId& vaultId, const CLoanSchemeData& scheme, const CBalances& collaterals, bool useNextPrice, bool requireLivePrice) const;
    Res CheckNextCollateralRatio(const CVaultId& vaultId, const CLoanSchemeData& scheme, const CBalances& collaterals) const;
};

#endif // DEFI_MASTERNODES_CONSENSUS_TXVISITOR_H
