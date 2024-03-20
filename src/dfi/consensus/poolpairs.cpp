// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/params.h>
#include <dfi/balances.h>
#include <dfi/consensus/poolpairs.h>
#include <dfi/masternodes.h>
#include <dfi/mn_checks.h>

Res CPoolPairsConsensus::EraseEmptyBalances(TAmounts &balances) const {
    auto &mnview = blockCtx.GetView();
    for (auto it = balances.begin(), next_it = it; it != balances.end(); it = next_it) {
        ++next_it;

        if (!mnview.GetToken(it->first)) {
            return Res::Err("reward token %d does not exist!", it->first.v);
        }

        if (it->second == 0) {
            balances.erase(it);
        }
    }
    return Res::Ok();
}

Res CPoolPairsConsensus::operator()(const CCreatePoolPairMessage &obj) const {
    // check foundation auth
    if (auto res = HasFoundationAuth(); !res) {
        return res;
    }
    if (obj.commission < 0 || obj.commission > COIN) {
        return Res::Err("wrong commission");
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto &tx = txCtx.GetTransaction();
    auto &mnview = blockCtx.GetView();

    if (height >= static_cast<uint32_t>(consensus.DF16FortCanningCrunchHeight)) {
        if (obj.pairSymbol.find('/') != std::string::npos) {
            return Res::Err("token symbol should not contain '/'");
        }
    }

    /// @todo ownerAddress validity checked only in rpc. is it enough?
    CPoolPair poolPair{};
    static_cast<CPoolPairMessageBase &>(poolPair) = obj;
    auto pairSymbol = obj.pairSymbol;
    poolPair.creationTx = tx.GetHash();
    poolPair.creationHeight = height;
    auto &rewards = poolPair.rewards;

    auto tokenA = mnview.GetToken(poolPair.idTokenA);
    if (!tokenA) {
        return Res::Err("token %s does not exist!", poolPair.idTokenA.ToString());
    }

    auto tokenB = mnview.GetToken(poolPair.idTokenB);
    if (!tokenB) {
        return Res::Err("token %s does not exist!", poolPair.idTokenB.ToString());
    }

    const auto symbolLength = height >= static_cast<uint32_t>(consensus.DF11FortCanningHeight)
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

    token.name = trim_ws(tokenA->name + "-" + tokenB->name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    token.symbol = pairSymbol;
    token.creationTx = tx.GetHash();
    token.creationHeight = height;

    // EVM Template will be null so no DST20 will be created
    BlockContext dummyContext{std::numeric_limits<uint32_t>::max(), {}, consensus};
    auto tokenId = mnview.CreateToken(token, dummyContext);
    if (!tokenId) {
        return tokenId;
    }

    rewards = obj.rewards;
    if (!rewards.balances.empty()) {
        // Check tokens exist and remove empty reward amounts
        if (auto res = EraseEmptyBalances(rewards.balances); !res) {
            return res;
        }
    }

    return mnview.SetPoolPair(tokenId, height, poolPair);
}

Res CPoolPairsConsensus::operator()(const CUpdatePoolPairMessage &obj) const {
    // check foundation auth
    if (auto res = HasFoundationAuth(); !res) {
        return res;
    }

    auto rewards = obj.rewards;
    if (!rewards.balances.empty()) {
        // Check for special case to wipe rewards
        if (!(rewards.balances.size() == 1 &&
              rewards.balances.cbegin()->first == DCT_ID{std::numeric_limits<uint32_t>::max()} &&
              rewards.balances.cbegin()->second == std::numeric_limits<CAmount>::max())) {
            // Check if tokens exist and remove empty reward amounts
            if (auto res = EraseEmptyBalances(rewards.balances); !res) {
                return res;
            }
        }
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();

    auto token = mnview.GetToken(obj.poolId);
    if (!token) {
        return Res::Err("Pool token %d does not exist\n", obj.poolId.v);
    }

    const auto tokenUpdated = !obj.pairSymbol.empty() || !obj.pairName.empty();
    if (tokenUpdated) {
        if (height < static_cast<uint32_t>(consensus.DF23Height)) {
            return Res::Err("Poolpair symbol cannot be changed below DF23 height");
        }
    }

    if (!obj.pairSymbol.empty()) {
        token->symbol = trim_ws(obj.pairSymbol).substr(0, CToken::MAX_TOKEN_POOLPAIR_LENGTH);
    }

    if (!obj.pairName.empty()) {
        token->name = trim_ws(obj.pairName).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    }

    if (tokenUpdated) {
        BlockContext dummyContext{std::numeric_limits<uint32_t>::max(), {}, Params().GetConsensus()};
        UpdateTokenContext ctx{*token, dummyContext, false, false, true};
        if (auto res = mnview.UpdateToken(ctx); !res) {
            return res;
        }
    }

    return mnview.UpdatePoolPair(obj.poolId, height, obj.status, obj.commission, obj.ownerAddress, rewards);
}

Res CPoolPairsConsensus::operator()(const CPoolSwapMessage &obj) const {
    // check auth
    if (auto res = HasAuth(obj.from); !res) {
        return res;
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();

    return CPoolSwap(obj, height).ExecuteSwap(mnview, {}, consensus);
}

Res CPoolPairsConsensus::operator()(const CPoolSwapMessageV2 &obj) const {
    // check auth
    if (auto res = HasAuth(obj.swapInfo.from); !res) {
        return res;
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();

    return CPoolSwap(obj.swapInfo, height).ExecuteSwap(mnview, obj.poolIDs, consensus);
}

Res CPoolPairsConsensus::operator()(const CLiquidityMessage &obj) const {
    CBalances sumTx = SumAllTransfers(obj.from);
    if (sumTx.balances.size() != 2) {
        return Res::Err("the pool pair requires two tokens");
    }

    std::pair<DCT_ID, CAmount> amountA = *sumTx.balances.begin();
    std::pair<DCT_ID, CAmount> amountB = *(std::next(sumTx.balances.begin(), 1));

    // checked internally too. remove here?
    if (amountA.second <= 0 || amountB.second <= 0) {
        return Res::Err("amount cannot be less than or equal to zero");
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();

    auto pair = mnview.GetPoolPair(amountA.first, amountB.first);
    if (!pair) {
        return Res::Err("there is no such pool pair");
    }

    for (const auto &kv : obj.from) {
        if (auto res = HasAuth(kv.first); !res) {
            return res;
        }
    }

    for (const auto &kv : obj.from) {
        CalculateOwnerRewards(kv.first);
        if (auto res = mnview.SubBalances(kv.first, kv.second); !res) {
            return res;
        }
    }

    const auto &lpTokenID = pair->first;
    auto &pool = pair->second;

    // normalize A & B to correspond poolpair's tokens
    if (amountA.first != pool.idTokenA) {
        std::swap(amountA, amountB);
    }

    bool slippageProtection = static_cast<int>(height) >= consensus.DF3BayfrontMarinaHeight;
    if (auto res = pool.AddLiquidity(
            amountA.second,
            amountB.second,
            [&] /*onMint*/ (CAmount liqAmount) {
                CBalances balance{TAmounts{{lpTokenID, liqAmount}}};
                return AddBalanceSetShares(obj.shareAddress, balance);
            },
            slippageProtection);
        !res) {
        return res;
    }

    return mnview.SetPoolPair(lpTokenID, height, pool);
}

Res CPoolPairsConsensus::operator()(const CRemoveLiquidityMessage &obj) const {
    const auto &from = obj.from;
    auto amount = obj.amount;

    // checked internally too. remove here?
    if (amount.nValue <= 0) {
        return Res::Err("amount cannot be less than or equal to zero");
    }

    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();

    auto pair = mnview.GetPoolPair(amount.nTokenId);
    if (!pair) {
        return Res::Err("there is no such pool pair");
    }

    if (auto res = HasAuth(from); !res) {
        return res;
    }

    CPoolPair &pool = pair.value();

    // subtract liq.balance BEFORE RemoveLiquidity call to check balance correctness
    CBalances balance{TAmounts{{amount.nTokenId, amount.nValue}}};
    if (auto res = SubBalanceDelShares(from, balance); !res) {
        return res;
    }

    if (auto res = pool.RemoveLiquidity(amount.nValue,
                                        [&](CAmount amountA, CAmount amountB) {
                                            CalculateOwnerRewards(from);
                                            CBalances balances{
                                                TAmounts{{pool.idTokenA, amountA}, {pool.idTokenB, amountB}}
                                            };
                                            return mnview.AddBalances(from, balances);
                                        });
        !res) {
        return res;
    }

    return mnview.SetPoolPair(amount.nTokenId, height, pool);
}
