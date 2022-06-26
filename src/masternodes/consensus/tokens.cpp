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

    auto& token = pair->second;

    if (token.IsDAT() != obj.isDAT && pair->first >= CTokensView::DCT_ID_START) {
        token.flags ^= (uint8_t)CToken::TokenFlags::DAT;
        //check foundation auth
        Require(HasFoundationAuth());
        return mnview.UpdateToken(token, true);
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

    CTokenImplementation updatedToken{obj.token};
    updatedToken.creationTx = token.creationTx;
    updatedToken.destructionTx = token.destructionTx;
    updatedToken.destructionHeight = token.destructionHeight;
    if (static_cast<int>(height) >= consensus.FortCanningHeight)
        updatedToken.symbol = trim_ws(updatedToken.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);

    return mnview.UpdateToken(updatedToken);
}

Res CTokensConsensus::operator()(const CMintTokensMessage& obj) const {
    // check auth and increase balance of token's owner
    for (const auto& [tokenId, amount] : obj.balances) {

        auto token = mnview.GetToken(tokenId);



        // ### START





        // ### END
        Require(token, "token %s does not exist!", tokenId.ToString());

        auto mintable = MintableToken(tokenId, *token);
        Require(mintable);

        if (height >= static_cast<uint32_t>(consensus.GreatWorldHeight) && token->IsDAT() && !HasFoundationAuth())
        {
            mintable.ok = false;

            auto attributes = mnview.GetAttributes();
            if (!attributes)
               return Res::Err("Cannot read from attributes gov variable!");

            CDataStructureV0 membersKey{AttributeTypes::Consortium, tokenId.v, ConsortiumKeys::Members};
            const auto members = attributes->GetValue(membersKey, CConsortiumMembers{});

            CDataStructureV0 membersMintedKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMembersMinted};
            auto membersBalances = attributes->GetValue(membersMintedKey, CConsortiumMembersMinted{});

            for (auto const& [key, member] : members)
            {
                if (HasAuth(member.ownerAddress))
                {
                    if (member.status != CConsortiumMember::Status::Active)
                        return Res::Err("Cannot mint token, not an active member of consortium for %s!", token->symbol);

                    auto add = SafeAdd(membersBalances[tokenId][key].minted, amount);
                    if (!add)
                        return (std::move(add));
                    membersBalances[tokenId][key].minted = add;

                    if (membersBalances[tokenId][key].minted > member.mintLimit)
                        return Res::Err("You will exceed your maximum mint limit for %s token by minting this amount!", token->symbol);

                    *mintable.val = member.ownerAddress;
                    mintable.ok = true;
                    break;
                }
            }

            if (!mintable)
                return Res::Err("You are not a foundation or consortium member and cannot mint this token!");

            CDataStructureV0 maxLimitKey{AttributeTypes::Consortium, tokenId.v, ConsortiumKeys::MintLimit};
            const auto maxLimit = attributes->GetValue(maxLimitKey, CAmount{0});

            CDataStructureV0 consortiumMintedKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMinted};
            auto globalBalances = attributes->GetValue(consortiumMintedKey, CConsortiumGlobalMinted{});

            auto add = SafeAdd(globalBalances[tokenId].minted, amount);
            if (!add)
                return (std::move(add));

            globalBalances[tokenId].minted = add;

            if (globalBalances[tokenId].minted > maxLimit)
                return Res::Err("You will exceed global maximum consortium mint limit for %s token by minting this amount!", token->symbol);

            attributes->SetValue(consortiumMintedKey, globalBalances);
            attributes->SetValue(membersMintedKey, membersBalances);

            auto saved = mnview.SetVariable(*attributes);
            if (!saved)
                return saved;
        }

        Require(mnview.AddMintedTokens(tokenId, amount));

        CalculateOwnerRewards(*mintable.val);
        Require(mnview.AddBalance(*mintable.val, CTokenAmount{tokenId, amount}));
    }

    return Res::Ok();
}

Res CTokensConsensus::operator()(const CBurnTokensMessage& obj) const {
    if (obj.amounts.balances.empty()) {
        return Res::Err("tx must have balances to burn");
    }

    for (const auto& [tokenId, amount] : obj.amounts.balances)
    {
        // check auth
        if (!HasAuth(obj.from))
            return Res::Err("tx must have at least one input from account owner");

        auto subMinted = mnview.SubMintedTokens(tokenId, amount);
        if (!subMinted)
            return subMinted;

        if (obj.burnType != CBurnTokensMessage::BurnType::TokenBurn)
            return Res::Err("Currently only burn type 0 - TokenBurn is supported!");

        CScript ownerAddress;

        if (auto address = std::get_if<CScript>(&obj.context); address && !address->empty())
            ownerAddress = *address;
        else ownerAddress = obj.from;

        auto attributes = mnview.GetAttributes();
        if (!attributes)
            return Res::Err("Cannot read from attributes gov variable!");

        CDataStructureV0 membersKey{AttributeTypes::Consortium, tokenId.v, ConsortiumKeys::Members};
        const auto members = attributes->GetValue(membersKey, CConsortiumMembers{});
        CDataStructureV0 membersMintedKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMembersMinted};
        auto membersBalances = attributes->GetValue(membersMintedKey, CConsortiumMembersMinted{});
        CDataStructureV0 consortiumMintedKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMinted};
        auto globalBalances = attributes->GetValue(consortiumMintedKey, CConsortiumGlobalMinted{});

        bool setVariable = false;
        for (auto const& tmp : members)
            if (tmp.second.ownerAddress == ownerAddress)
            {
                auto add = SafeAdd(membersBalances[tokenId][tmp.first].burnt, amount);
                if (!add)
                    return (std::move(add));

                membersBalances[tokenId][tmp.first].burnt = add;

                add = SafeAdd(globalBalances[tokenId].burnt, amount);
                if (!add)
                    return (std::move(add));

                globalBalances[tokenId].burnt = add;

                setVariable = true;
                break;
            }

        if (setVariable)
        {
            attributes->SetValue(membersMintedKey, membersBalances);
            attributes->SetValue(consortiumMintedKey, globalBalances);

            auto saved = mnview.SetVariable(*attributes);
            if (!saved)
                return saved;
        }

        CalculateOwnerRewards(obj.from);

        auto res = TransferTokenBalance(tokenId, amount, obj.from, consensus.burnAddress);
        if (!res)
            return res;
    }

    return Res::Ok();

}
