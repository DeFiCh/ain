// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <coins.h>
#include <consensus/params.h>
#include <masternodes/consensus/tokens.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/gv.h>
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
        CAmount amount = kv.second;

        auto token = mnview.GetToken(tokenId);
        if (!token)
            return Res::Err("token %s does not exist!", tokenId.ToString());

        auto tokenImpl = static_cast<const CTokenImplementation&>(*token);

        auto mintable = MintableToken(tokenId, tokenImpl);
        if (!mintable)
            return std::move(mintable);

        if (static_cast<int>(height) >= consensus.GreatWorldHeight && token->IsDAT() && !HasFoundationAuth())
        {
            mintable.ok = false;

            auto attributes = mnview.GetAttributes();

            CDataStructureV0 membersKey{AttributeTypes::Token, tokenId.v, TokenKeys::ConsortiumMembers};
            auto members = attributes->GetValue(membersKey, CConsortiumMembers{});

            CDataStructureV0 membersMintedKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMembersMinted};
            auto membersBalances = attributes->GetValue(membersMintedKey, CConsortiumMembersMinted{});

            for (size_t i=0; i < members.size(); i++)
            {
                auto member = members[i];

                if (HasAuth(member.ownerAddress))
                {
                    if (membersBalances[i].minted.balances[tokenId] + amount > member.mintLimit)
                        return Res::Err("You will exceed your maximum mint limit for %s token by minting this amount!", tokenImpl.symbol);

                    membersBalances[i].minted.Add(CTokenAmount{tokenId, amount});
                    *mintable.val = member.ownerAddress;
                    mintable.ok = true;
                    attributes->SetValue(membersMintedKey, membersBalances);
                    break;
                }
            }

            if (!mintable)
                return Res::Err("You are not a foundation or consortium member and cannot mint this token!");

            CDataStructureV0 maxLimitKey{AttributeTypes::Token, tokenId.v, TokenKeys::ConsortiumMintLimit};
            auto maxLimit = attributes->GetValue(maxLimitKey, CAmount{0});

            CDataStructureV0 consortiumMintedKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMinted};
            auto globalBalnaces = attributes->GetValue(consortiumMintedKey, CBalances{});

            if (globalBalnaces.balances[tokenId] + amount > maxLimit)
                return Res::Err("You will exceed global maximum mint limit for %s token by minting this amount!", tokenImpl.symbol);

            globalBalnaces.Add(CTokenAmount{tokenId, amount});
            attributes->SetValue(consortiumMintedKey, globalBalnaces);

            auto saved = mnview.SetVariable(*attributes);
            if (!saved)
                return saved;
        }

        auto minted = mnview.AddMintedTokens(tokenId, amount);
        if (!minted)
            return minted;

        CalculateOwnerRewards(*mintable.val);
        auto res = mnview.AddBalance(*mintable.val, CTokenAmount{tokenId, amount});
        if (!res)
            return res;
    }

    return Res::Ok();
}
