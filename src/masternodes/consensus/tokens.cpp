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
    Require(CheckTokenCreationTx());

    CTokenImplementation token;
    static_cast<CToken&>(token) = obj;

    token.symbol = trim_ws(token.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    token.name = trim_ws(token.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    token.creationTx = tx.GetHash();
    token.creationHeight = height;

    // check foundation auth
    if (token.IsDAT())
        Require(HasFoundationAuth());

    if (static_cast<int>(height) >= consensus.BayfrontHeight) // formal compatibility if someone cheat and create LPS token on the pre-bayfront node
        Require(!token.IsPoolShare(), "Cant't manually create 'Liquidity Pool Share' token; use poolpair creation");

    return mnview.CreateToken(token, static_cast<int>(height) < consensus.BayfrontHeight);
}

Res CTokensConsensus::operator()(const CUpdateTokenPreAMKMessage& obj) const {
    auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
    Require(pair, "token with creationTx %s does not exist", obj.tokenTx.ToString());

    const auto& token = pair->second;

    //check foundation auth
    Require(HasFoundationAuth());

    if (token.IsDAT() != obj.isDAT && pair->first >= CTokensView::DCT_ID_START) {
        CToken newToken = static_cast<CToken>(token); // keeps old and triggers only DAT!
        newToken.flags ^= (uint8_t)CToken::TokenFlags::DAT;
        return mnview.UpdateToken(token.creationTx, newToken, true);
    }
    return Res::Ok();
}

Res CTokensConsensus::operator()(const CUpdateTokenMessage& obj) const {
    auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
    Require(pair, "token with creationTx %s does not exist", obj.tokenTx.ToString());
    Require(pair->first != DCT_ID{0}, "Can't alter DFI token!");

    Require(!mnview.AreTokensLocked({pair->first.v}), "Cannot update token during lock");

    const auto& token = pair->second;

    // need to check it exectly here cause lps has no collateral auth (that checked next)
    Require(!token.IsPoolShare(), "token %s is the LPS token! Can't alter pool share's tokens!", obj.tokenTx.ToString());

    // check auth, depends from token's "origins"
    const Coin& auth = coins.AccessCoin(COutPoint(token.creationTx, 1)); // always n=1 output
    bool isFoundersToken = consensus.foundationMembers.count(auth.out.scriptPubKey) > 0;

    if (isFoundersToken)
        Require(HasFoundationAuth());
    else
        Require(HasCollateralAuth(token.creationTx));

    // Check for isDAT change in non-foundation token after set height
    if (static_cast<int>(height) >= consensus.BayfrontMarinaHeight)
        //check foundation auth
        Require(obj.token.IsDAT() == token.IsDAT() || HasFoundationAuth(), "can't set isDAT to true, tx not from foundation member");

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
        Require(token, "token %s does not exist!", tokenId.ToString());

        auto mintable = MintableToken(tokenId, *token);
        Require(mintable);

        Require(mnview.AddMintedTokens(tokenId, kv.second));

        CalculateOwnerRewards(*mintable);
        Require(mnview.AddBalance(*mintable, CTokenAmount{kv.first, kv.second}));
    }
    return Res::Ok();
}
