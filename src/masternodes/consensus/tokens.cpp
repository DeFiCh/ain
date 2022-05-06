// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <coins.h>
#include <consensus/params.h>
#include <masternodes/consensus/tokens.h>
#include <masternodes/masternodes.h>
#include <masternodes/tokens.h>
#include <primitives/transaction.h>

Res CTokensConsensus::operator()(const CCreateTokenMessage& obj) const {
    auto res = CheckTokenCreationTx();
    if (!res)
        return res;

    CTokenImplementation token;
    static_cast<CToken&>(token) = obj;

    token.symbol = trim_ws(token.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    token.name = trim_ws(token.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    token.creationTx = tx.GetHash();
    token.creationHeight = height;

    // check foundation auth
    if (token.IsDAT() && !HasFoundationAuth())
        return Res::Err("tx not from foundation member");

    if (static_cast<int>(height) >= consensus.BayfrontHeight) // formal compatibility if someone cheat and create LPS token on the pre-bayfront node
        if (token.IsPoolShare())
            return Res::Err("Cant't manually create 'Liquidity Pool Share' token; use poolpair creation");

    return mnview.CreateToken(token, static_cast<int>(height) < consensus.BayfrontHeight);
}

Res CTokensConsensus::operator()(const CUpdateTokenPreAMKMessage& obj) const {
    auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
    if (!pair)
        return Res::Err("token with creationTx %s does not exist", obj.tokenTx.ToString());

    const auto& token = pair->second;

    //check foundation auth
    auto res = HasFoundationAuth();

    if (token.IsDAT() != obj.isDAT && pair->first >= CTokensView::DCT_ID_START) {
        CToken newToken = static_cast<CToken>(token); // keeps old and triggers only DAT!
        newToken.flags ^= (uint8_t)CToken::TokenFlags::DAT;
        return !res ? res : mnview.UpdateToken(token.creationTx, newToken, true);
    }
    return res;
}

Res CTokensConsensus::operator()(const CUpdateTokenMessage& obj) const {
    auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
    if (!pair)
        return Res::Err("token with creationTx %s does not exist", obj.tokenTx.ToString());

    if (pair->first == DCT_ID{0})
        return Res::Err("Can't alter DFI token!"); // may be redundant cause DFI is 'finalized'

    if (mnview.AreTokensLocked({pair->first.v})) {
        return Res::Err("Cannot update token during lock");
    }

    const auto& token = pair->second;

    // need to check it exectly here cause lps has no collateral auth (that checked next)
    if (token.IsPoolShare())
        return Res::Err("token %s is the LPS token! Can't alter pool share's tokens!", obj.tokenTx.ToString());

    // check auth, depends from token's "origins"
    const Coin& auth = coins.AccessCoin(COutPoint(token.creationTx, 1)); // always n=1 output
    bool isFoundersToken = consensus.foundationMembers.count(auth.out.scriptPubKey) > 0;

    auto res = Res::Ok();
    if (isFoundersToken && !(res = HasFoundationAuth()))
        return res;
    else if (!(res = HasCollateralAuth(token.creationTx)))
        return res;

    // Check for isDAT change in non-foundation token after set height
    if (static_cast<int>(height) >= consensus.BayfrontMarinaHeight)
        //check foundation auth
        if (obj.token.IsDAT() != token.IsDAT() && !HasFoundationAuth()) //no need to check Authority if we don't create isDAT
            return Res::Err("can't set isDAT to true, tx not from foundation member");

    auto updatedToken = obj.token;
    if (static_cast<int>(height) >= consensus.FortCanningHeight)
        updatedToken.symbol = trim_ws(updatedToken.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);

    return mnview.UpdateToken(token.creationTx, updatedToken, false);
}

Res CTokensConsensus::operator()(const CMintTokensMessage& obj) const {
    // check auth and increase balance of token's owner
    for (const auto& kv : obj.balances) {
        DCT_ID tokenId = kv.first;

        auto token = mnview.GetToken(tokenId);
        if (!token)
            return Res::Err("token %s does not exist!", tokenId.ToString());

        auto tokenImpl = static_cast<const CTokenImplementation&>(*token);

        auto mintable = MintableToken(tokenId, tokenImpl);
        if (!mintable)
            return std::move(mintable);

        auto minted = mnview.AddMintedTokens(tokenId, kv.second);
        if (!minted)
            return minted;

        CalculateOwnerRewards(*mintable.val);
        auto res = mnview.AddBalance(*mintable.val, CTokenAmount{kv.first, kv.second});
        if (!res)
            return res;
    }
    return Res::Ok();
}
