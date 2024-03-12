// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_DFI_CONSENSUS_TXVISITOR_H
#define DEFI_DFI_CONSENSUS_TXVISITOR_H

#include <amount.h>
#include <dfi/balances.h>
#include <dfi/oracles.h>
#include <dfi/res.h>
#include <dfi/vault.h>

class BlockContext;
struct CBalances;
class CCoinsViewCache;
struct CCreateProposalMessage;
class CCustomCSView;
struct CLoanSchemeData;
class CPoolPair;
class CScript;
class CScopedTemplate;
class CTokenImplementation;
class CTransaction;
class CVaultAssets;
class TransactionContext;

namespace Consensus {
    struct Params;
}

enum AuthStrategy : uint32_t {
    DirectPubKeyMatch,
    Mapped,
};

namespace AuthFlags {
    enum Type : uint32_t {
        None = 0,
        Bech32InSource = 1 << 1,
        PKHashInSource = 1 << 2,
    };
}

Res HasAuth(const CTransaction &tx,
            const CCoinsViewCache &coins,
            const CScript &auth,
            AuthStrategy strategy = AuthStrategy::DirectPubKeyMatch,
            AuthFlags::Type flags = AuthFlags::None);
Res GetERC55AddressFromAuth(const CTransaction &tx, const CCoinsViewCache &coins, CScript &script);

class CCustomTxVisitor {
protected:
    BlockContext &blockCtx;
    const TransactionContext &txCtx;

public:
    CCustomTxVisitor(BlockContext &blockCtx, const TransactionContext &txCtx);

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
    Res CollateralPctCheck(const bool hasDUSDLoans, const CVaultAssets &collateralsLoans, const uint32_t ratio) const;
    ResVal<CVaultAssets> CheckCollateralRatio(const CVaultId &vaultId,
                                              const CLoanSchemeData &scheme,
                                              const CBalances &collaterals,
                                              bool useNextPrice,
                                              bool requireLivePrice) const;
};

#endif  // DEFI_DFI_CONSENSUS_TXVISITOR_H
