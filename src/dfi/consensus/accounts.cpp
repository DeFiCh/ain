// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <dfi/accounts.h>
#include <dfi/balances.h>
#include <dfi/consensus/accounts.h>
#include <dfi/mn_checks.h>
#include <primitives/transaction.h>
#include <validation.h>

static ResVal<CBalances> BurntTokens(const CTransaction &tx) {
    CBalances balances;
    for (const auto &out : tx.vout) {
        if (out.scriptPubKey.size() > 0 && out.scriptPubKey[0] == OP_RETURN) {
            if (auto res = balances.Add(out.TokenAmount()); !res) {
                return res;
            }
        }
    }
    return {balances, Res::Ok()};
}

Res CAccountsConsensus::operator()(const CUtxosToAccountMessage &obj) const {
    const auto &tx = txCtx.GetTransaction();

    // check enough tokens are "burnt"
    auto burnt = BurntTokens(tx);
    if (!burnt) {
        return burnt;
    }

    const auto mustBeBurnt = SumAllTransfers(obj.to);
    if (*burnt.val != mustBeBurnt) {
        return Res::Err(
            "transfer tokens mismatch burnt tokens: (%s) != (%s)", mustBeBurnt.ToString(), burnt.val->ToString());
    }

    // transfer
    return AddBalancesSetShares(obj.to);
}

Res CAccountsConsensus::operator()(const CAccountToUtxosMessage &obj) const {
    // check auth
    if (auto res = HasAuth(obj.from); !res) {
        return res;
    }

    // check that all tokens are minted, and no excess tokens are minted
    auto minted = MintedTokens(obj.mintingOutputsStart);
    if (!minted) {
        return minted;
    }

    if (obj.balances != *minted.val) {
        return Res::Err("amount of minted tokens in UTXOs and metadata do not match: (%s) != (%s)",
                        minted.val->ToString(),
                        obj.balances.ToString());
    }

    // block for non-DFI transactions
    for (const auto &kv : obj.balances.balances) {
        const DCT_ID &tokenId = kv.first;
        if (tokenId != DCT_ID{0}) {
            return Res::Err("only available for DFI transactions");
        }
    }

    // transfer
    return SubBalanceDelShares(obj.from, obj.balances);
}

Res CAccountsConsensus::operator()(const CAccountToAccountMessage &obj) const {
    // check auth
    if (auto res = HasAuth(obj.from); !res) {
        return res;
    }

    // transfer
    if (auto res = SubBalanceDelShares(obj.from, SumAllTransfers(obj.to)); !res) {
        return res;
    }

    return AddBalancesSetShares(obj.to);
}

Res CAccountsConsensus::operator()(const CAnyAccountsToAccountsMessage &obj) const {
    // check auth
    for (const auto &kv : obj.from) {
        if (auto res = HasAuth(kv.first); !res) {
            return res;
        }
    }

    // compare
    const auto sumFrom = SumAllTransfers(obj.from);
    const auto sumTo = SumAllTransfers(obj.to);

    if (sumFrom != sumTo) {
        return Res::Err("sum of inputs (from) != sum of outputs (to)");
    }

    // transfer
    // subtraction
    if (auto res = SubBalancesDelShares(obj.from); !res) {
        return res;
    }
    // addition
    return AddBalancesSetShares(obj.to);
}
