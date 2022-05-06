// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accounts.h>
#include <masternodes/balances.h>
#include <masternodes/consensus/accounts.h>

Res CAccountsConsensus::operator()(const CUtxosToAccountMessage& obj) const {
    // check enough tokens are "burnt"
    const auto burnt = BurntTokens();
    if (!burnt)
        return burnt;

    const auto mustBeBurnt = SumAllTransfers(obj.to);
    if (*burnt.val != mustBeBurnt)
        return Res::Err("transfer tokens mismatch burnt tokens: (%s) != (%s)", mustBeBurnt.ToString(), burnt.val->ToString());

    // transfer
    return AddBalancesSetShares(obj.to);
}

Res CAccountsConsensus::operator()(const CAccountToUtxosMessage& obj) const {
    // check auth
    if (!HasAuth(obj.from))
        return Res::Err("tx must have at least one input from account owner");

    // check that all tokens are minted, and no excess tokens are minted
    auto minted = MintedTokens(obj.mintingOutputsStart);
    if (!minted)
        return std::move(minted);

    if (obj.balances != *minted)
        return Res::Err("amount of minted tokens in UTXOs and metadata do not match: (%s) != (%s)", minted.val->ToString(), obj.balances.ToString());

    // block for non-DFI transactions
    for (const auto& kv : obj.balances.balances)
        if (kv.first != DCT_ID{0})
            return Res::Err("only available for DFI transactions");

    // transfer
    return SubBalanceDelShares(obj.from, obj.balances);
}

Res CAccountsConsensus::operator()(const CAccountToAccountMessage& obj) const {
    // check auth
    if (!HasAuth(obj.from))
        return Res::Err("tx must have at least one input from account owner");

    // transfer
    auto res = SubBalanceDelShares(obj.from, SumAllTransfers(obj.to));
    return !res ? res : AddBalancesSetShares(obj.to);
}

Res CAccountsConsensus::operator()(const CAnyAccountsToAccountsMessage& obj) const {
    // check auth
    for (const auto& kv : obj.from)
        if (!HasAuth(kv.first))
            return Res::Err("tx must have at least one input from account owner");

    // compare
    const auto sumFrom = SumAllTransfers(obj.from);
    const auto sumTo = SumAllTransfers(obj.to);

    if (sumFrom != sumTo)
        return Res::Err("sum of inputs (from) != sum of outputs (to)");

    // transfer
    // substraction
    auto res = SubBalancesDelShares(obj.from);
    // addition
    return !res ? res : AddBalancesSetShares(obj.to);
}
