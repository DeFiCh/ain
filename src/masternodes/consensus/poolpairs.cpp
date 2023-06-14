// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/params.h>
#include <masternodes/balances.h>
#include <masternodes/consensus/poolpairs.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_checks.h>

Res CPoolPairsConsensus::EraseEmptyBalances(TAmounts &balances) const {
    for (auto it = balances.begin(), next_it = it; it != balances.end(); it = next_it) {
        ++next_it;

        Require(mnview.GetToken(it->first), "reward token %d does not exist!", it->first.v);

        if (it->second == 0) {
            balances.erase(it);
        }
    }
    return Res::Ok();
}

Res CPoolPairsConsensus::operator()(const CCreatePoolPairMessage &obj) const {
    // check foundation auth
    Require(HasFoundationAuth());
    Require(obj.commission >= 0 && obj.commission <= COIN, "wrong commission");

    if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningCrunchHeight)) {
        Require(obj.pairSymbol.find('/') == std::string::npos, "token symbol should not contain '/'");
    }

    /// @todo ownerAddress validity checked only in rpc. is it enough?
    CPoolPair poolPair{};
    static_cast<CPoolPairMessageBase&>(poolPair) = obj;
    auto pairSymbol         = obj.pairSymbol;
    poolPair.creationTx     = tx.GetHash();
    poolPair.creationHeight = height;
    auto &rewards           = poolPair.rewards;

    auto tokenA = mnview.GetToken(poolPair.idTokenA);
    Require(tokenA, "token %s does not exist!", poolPair.idTokenA.ToString());

    auto tokenB = mnview.GetToken(poolPair.idTokenB);
    Require(tokenB, "token %s does not exist!", poolPair.idTokenB.ToString());

    const auto symbolLength = height >= static_cast<uint32_t>(consensus.FortCanningHeight)
                              ? CToken::MAX_TOKEN_POOLPAIR_LENGTH
                              : CToken::MAX_TOKEN_SYMBOL_LENGTH;
    if (pairSymbol.empty()) {
        pairSymbol = trim_ws(tokenA->symbol + "-" + tokenB->symbol).substr(0, symbolLength);
    } else {
        pairSymbol = trim_ws(pairSymbol).substr(0, symbolLength);
    }

    CTokenImplementation token;
    token.flags = (uint8_t)CToken::TokenFlags::DAT | (uint8_t)CToken::TokenFlags::LPS |
                  (uint8_t)CToken::TokenFlags::Tradeable | (uint8_t)CToken::TokenFlags::Finalized;

    token.name           = trim_ws(tokenA->name + "-" + tokenB->name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    token.symbol         = pairSymbol;
    token.creationTx     = tx.GetHash();
    token.creationHeight = height;

    auto tokenId = mnview.CreateToken(token);
    Require(tokenId);

    rewards = obj.rewards;
    if (!rewards.balances.empty()) {
        // Check tokens exist and remove empty reward amounts
        Require(EraseEmptyBalances(rewards.balances));
    }

    return mnview.SetPoolPair(tokenId, height, poolPair);
}

Res CPoolPairsConsensus::operator()(const CUpdatePoolPairMessage &obj) const {
    // check foundation auth
    Require(HasFoundationAuth());

    auto rewards = obj.rewards;
    if (!rewards.balances.empty()) {
        // Check for special case to wipe rewards
        if (!(rewards.balances.size() == 1 &&
              rewards.balances.cbegin()->first == DCT_ID{std::numeric_limits<uint32_t>::max()} &&
              rewards.balances.cbegin()->second == std::numeric_limits<CAmount>::max())) {
            // Check if tokens exist and remove empty reward amounts
            Require(EraseEmptyBalances(rewards.balances));
        }
    }
    return mnview.UpdatePoolPair(obj.poolId, height, obj.status, obj.commission, obj.ownerAddress, rewards);
}

Res CPoolPairsConsensus::operator()(const CPoolSwapMessage &obj) const {
    // check auth
    Require(HasAuth(obj.from));

    return CPoolSwap(obj, height).ExecuteSwap(mnview, {});
}

Res CPoolPairsConsensus::operator()(const CPoolSwapMessageV2 &obj) const {
    // check auth
    Require(HasAuth(obj.swapInfo.from));

    return CPoolSwap(obj.swapInfo, height).ExecuteSwap(mnview, obj.poolIDs);
}

Res CPoolPairsConsensus::operator()(const CLiquidityMessage &obj) const {
    CBalances sumTx = SumAllTransfers(obj.from);
    Require(sumTx.balances.size() == 2, "the pool pair requires two tokens");

    std::pair<DCT_ID, CAmount> amountA = *sumTx.balances.begin();
    std::pair<DCT_ID, CAmount> amountB = *(std::next(sumTx.balances.begin(), 1));

    // checked internally too. remove here?
    Require(amountA.second > 0 && amountB.second > 0, "amount cannot be less than or equal to zero");

    auto pair = mnview.GetPoolPair(amountA.first, amountB.first);
    Require(pair, "there is no such pool pair");

    for (const auto &kv : obj.from) {
        Require(HasAuth(kv.first));
    }

    for (const auto &kv : obj.from) {
        CalculateOwnerRewards(kv.first);
        Require(mnview.SubBalances(kv.first, kv.second));
    }

    const auto &lpTokenID = pair->first;
    auto &pool            = pair->second;

    // normalize A & B to correspond poolpair's tokens
    if (amountA.first != pool.idTokenA)
        std::swap(amountA, amountB);

    bool slippageProtection = static_cast<int>(height) >= consensus.BayfrontMarinaHeight;
    Require(pool.AddLiquidity(
            amountA.second,
            amountB.second,
            [&] /*onMint*/ (CAmount liqAmount) {
                CBalances balance{TAmounts{{lpTokenID, liqAmount}}};
                return AddBalanceSetShares(obj.shareAddress, balance);
            },
            slippageProtection));
    return mnview.SetPoolPair(lpTokenID, height, pool);
}

Res CPoolPairsConsensus::operator()(const CRemoveLiquidityMessage &obj) const {
    const auto &from = obj.from;
    auto amount      = obj.amount;

    // checked internally too. remove here?
    Require(amount.nValue > 0, "amount cannot be less than or equal to zero");

    auto pair = mnview.GetPoolPair(amount.nTokenId);
    Require(pair, "there is no such pool pair");

    Require(HasAuth(from));

    CPoolPair &pool = pair.value();

    // subtract liq.balance BEFORE RemoveLiquidity call to check balance correctness
    CBalances balance{TAmounts{{amount.nTokenId, amount.nValue}}};
    Require(SubBalanceDelShares(from, balance));

    Require(pool.RemoveLiquidity(amount.nValue, [&](CAmount amountA, CAmount amountB) {
        CalculateOwnerRewards(from);
        CBalances balances{
                TAmounts{{pool.idTokenA, amountA}, {pool.idTokenB, amountB}}
        };
        return mnview.AddBalances(from, balances);
    }));

    return mnview.SetPoolPair(amount.nTokenId, height, pool);
}
