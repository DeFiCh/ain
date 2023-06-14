// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accounts.h>
#include <masternodes/balances.h>
#include <masternodes/consensus/accounts.h>
#include <primitives/transaction.h>

ResVal<CBalances> CAccountsConsensus::BurntTokens() const {
    CBalances balances;
    for (const auto &out : tx.vout) {
        if (out.scriptPubKey.size() > 0 && out.scriptPubKey[0] == OP_RETURN) {
            Require(balances.Add(out.TokenAmount()));
        }
    }
    return {balances, Res::Ok()};
}

Res CAccountsConsensus::operator()(const CUtxosToAccountMessage &obj) const {
    // check enough tokens are "burnt"
    auto burnt = BurntTokens();
    Require(burnt);

    const auto mustBeBurnt = SumAllTransfers(obj.to);
    Require(*burnt.val == mustBeBurnt,
            "transfer tokens mismatch burnt tokens: (%s) != (%s)",
            mustBeBurnt.ToString(),
            burnt.val->ToString());

    // transfer
    return AddBalancesSetShares(obj.to);
}

Res CAccountsConsensus::operator()(const CAccountToUtxosMessage &obj) const {
    // check auth
    Require(HasAuth(obj.from));

    // check that all tokens are minted, and no excess tokens are minted
    auto minted = MintedTokens(obj.mintingOutputsStart);
    Require(minted);

    Require(obj.balances == *minted.val,
            "amount of minted tokens in UTXOs and metadata do not match: (%s) != (%s)",
            minted.val->ToString(),
            obj.balances.ToString());

    // block for non-DFI transactions
    for (const auto &kv : obj.balances.balances) {
        const DCT_ID &tokenId = kv.first;
        Require(tokenId == DCT_ID{0}, "only available for DFI transactions");
    }

    // transfer
    return SubBalanceDelShares(obj.from, obj.balances);
}

Res CAccountsConsensus::operator()(const CAccountToAccountMessage &obj) const {
    // check auth
    Require(HasAuth(obj.from));

    // transfer
    Require(SubBalanceDelShares(obj.from, SumAllTransfers(obj.to)));
    return AddBalancesSetShares(obj.to);
}


Res CAccountsConsensus::operator()(const CAnyAccountsToAccountsMessage &obj) const {
    // check auth
    for (const auto &kv : obj.from) {
        Require(HasAuth(kv.first));
    }

    // compare
    const auto sumFrom = SumAllTransfers(obj.from);
    const auto sumTo   = SumAllTransfers(obj.to);

    Require(sumFrom == sumTo, "sum of inputs (from) != sum of outputs (to)");

    // transfer
    // substraction
    Require(SubBalancesDelShares(obj.from));
    // addition
    return AddBalancesSetShares(obj.to);
}
