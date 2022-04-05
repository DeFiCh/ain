// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/params.h>
#include <masternodes/balances.h>
#include <masternodes/consensus/poolpairs.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_checks.h>
#include <masternodes/poolpairs.h>
#include <masternodes/tokens.h>

Res CPoolPairsConsensus::operator()(const CCreatePoolPairMessage& obj) const {

    if (static_cast<int>(height) < consensus.ClarkeQuayHeight && !obj.rewards.balances.empty())
        return Res::Err("rewards are not active");

    //check foundation auth
    if (!HasFoundationAuth())
        return Res::Err("tx not from foundation member");

    if (obj.commission < 0 || obj.commission > COIN)
        return Res::Err("wrong commission");

    /// @todo ownerAddress validity checked only in rpc. is it enough?
    CPoolPair poolPair{};
    static_cast<CPoolPairMessageBase&>(poolPair) = obj;
    auto pairSymbol = obj.pairSymbol;
    poolPair.creationTx = tx.GetHash();
    poolPair.creationHeight = height;
    auto& rewards = poolPair.rewards;

    auto tokenA = mnview.GetToken(poolPair.idTokenA);
    if (!tokenA)
        return Res::Err("token %s does not exist!", poolPair.idTokenA.ToString());

    auto tokenB = mnview.GetToken(poolPair.idTokenB);
    if (!tokenB)
        return Res::Err("token %s does not exist!", poolPair.idTokenB.ToString());

    const auto symbolLength = static_cast<int>(height) >= consensus.FortCanningHeight ? CToken::MAX_TOKEN_POOLPAIR_LENGTH : CToken::MAX_TOKEN_SYMBOL_LENGTH;
    if (pairSymbol.empty())
        pairSymbol = trim_ws(tokenA->symbol + '-' + tokenB->symbol).substr(0, symbolLength);
    else
        pairSymbol = trim_ws(pairSymbol).substr(0, symbolLength);

    CTokenImplementation token;
    token.flags = (uint8_t)CToken::TokenFlags::DAT |
                  (uint8_t)CToken::TokenFlags::LPS |
                  (uint8_t)CToken::TokenFlags::Tradeable |
                  (uint8_t)CToken::TokenFlags::Finalized;

    token.name = trim_ws(tokenA->name + '-' + tokenB->name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    token.symbol = pairSymbol;
    token.creationTx = tx.GetHash();
    token.creationHeight = height;

    auto tokenId = mnview.CreateToken(token, false);
    if (!tokenId)
        return std::move(tokenId);

    rewards = obj.rewards;
    if (!rewards.balances.empty()) {
        // Check tokens exist and remove empty reward amounts
        auto res = EraseEmptyBalances(rewards.balances);
        if (!res)
            return res;
    }

    return mnview.SetPoolPair(tokenId, height, poolPair);
}

Res CPoolPairsConsensus::operator()(const CUpdatePoolPairMessage& obj) const {

    if (static_cast<int>(height) < consensus.ClarkeQuayHeight && !obj.rewards.balances.empty())
        return Res::Err("rewards are not active");

    //check foundation auth
    if (!HasFoundationAuth())
        return Res::Err("tx not from foundation member");

    auto rewards = obj.rewards;
    if (!rewards.balances.empty()) {
        // Check for special case to wipe rewards
        if (!(rewards.balances.size() == 1 && rewards.balances.cbegin()->first == DCT_ID{std::numeric_limits<uint32_t>::max()}
        && rewards.balances.cbegin()->second == std::numeric_limits<CAmount>::max())) {
            // Check if tokens exist and remove empty reward amounts
            auto res = EraseEmptyBalances(rewards.balances);
            if (!res)
                return res;
        }
    }
    return mnview.UpdatePoolPair(obj.poolId, height, obj.status, obj.commission, obj.ownerAddress, rewards);
}

Res CPoolPairsConsensus::operator()(const CPoolSwapMessage& obj) const {
    // check auth
    if (!HasAuth(obj.from))
        return Res::Err("tx must have at least one input from account owner");

    return CPoolSwap(obj, height).ExecuteSwap(mnview, {});
}

Res CPoolPairsConsensus::operator()(const CPoolSwapMessageV2& obj) const {
    // check auth
    if (!HasAuth(obj.swapInfo.from))
        return Res::Err("tx must have at least one input from account owner");

    if (height >= static_cast<uint32_t>(consensus.FortCanningHillHeight) && obj.poolIDs.size() > 3)
        return Res::Err(strprintf("Too many pool IDs provided, max 3 allowed, %d provided", obj.poolIDs.size()));

    return CPoolSwap(obj.swapInfo, height).ExecuteSwap(mnview, obj.poolIDs);
}

Res CPoolPairsConsensus::operator()(const CLiquidityMessage& obj) const {
    CBalances sumTx = SumAllTransfers(obj.from);
    if (sumTx.balances.size() != 2)
        return Res::Err("the pool pair requires two tokens");

    std::pair<DCT_ID, CAmount> amountA = *sumTx.balances.begin();
    std::pair<DCT_ID, CAmount> amountB = *(std::next(sumTx.balances.begin(), 1));

    // checked internally too. remove here?
    if (amountA.second <= 0 || amountB.second <= 0)
        return Res::Err("amount cannot be less than or equal to zero");

    auto pair = mnview.GetPoolPair(amountA.first, amountB.first);
    if (!pair)
        return Res::Err("there is no such pool pair");

    for (const auto& kv : obj.from)
        if (!HasAuth(kv.first))
            return Res::Err("tx must have at least one input from account owner");

    for (const auto& kv : obj.from) {
        CalculateOwnerRewards(kv.first);
        auto res = mnview.SubBalances(kv.first, kv.second);
        if (!res)
            return res;
    }

    const auto& lpTokenID = pair->first;
    auto& pool = pair->second;

    // normalize A & B to correspond poolpair's tokens
    if (amountA.first != pool.idTokenA)
        std::swap(amountA, amountB);

    bool slippageProtection = static_cast<int>(height) >= consensus.BayfrontMarinaHeight;
    auto res = pool.AddLiquidity(amountA.second, amountB.second, [&] /*onMint*/(CAmount liqAmount) {

        CBalances balance{TAmounts{{lpTokenID, liqAmount}}};
        return AddBalanceSetShares(obj.shareAddress, balance);
    }, slippageProtection);

    return !res ? res : mnview.SetPoolPair(lpTokenID, height, pool);
}

Res CPoolPairsConsensus::operator()(const CRemoveLiquidityMessage& obj) const {
    const auto& from = obj.from;
    auto amount = obj.amount;

    // checked internally too. remove here?
    if (amount.nValue <= 0)
        return Res::Err("amount cannot be less than or equal to zero");

    auto pair = mnview.GetPoolPair(amount.nTokenId);
    if (!pair)
        return Res::Err("there is no such pool pair");

    if (!HasAuth(from))
        return Res::Err("tx must have at least one input from account owner");

    CPoolPair& pool = *pair;
    // subtract liq.balance BEFORE RemoveLiquidity call to check balance correctness
    CBalances balance{TAmounts{{amount.nTokenId, amount.nValue}}};
    auto res = SubBalanceDelShares(from, balance);
    if (!res)
        return res;

    res = pool.RemoveLiquidity(amount.nValue, [&] (CAmount amountA, CAmount amountB) {

        CalculateOwnerRewards(from);
        CBalances balances{TAmounts{{pool.idTokenA, amountA}, {pool.idTokenB, amountB}}};
        return mnview.AddBalances(from, balances);
    });

    return !res ? res : mnview.SetPoolPair(amount.nTokenId, height, pool);
}
