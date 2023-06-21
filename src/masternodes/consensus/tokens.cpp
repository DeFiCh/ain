// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <coins.h>
#include <consensus/params.h>
#include <masternodes/consensus/tokens.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_checks.h>


Res CTokensConsensus::CheckTokenCreationTx() const {
    Require(tx.vout.size() >= 2 && tx.vout[0].nValue >= GetTokenCreationFee(height) &&
            tx.vout[0].nTokenId == DCT_ID{0} && tx.vout[1].nValue == GetTokenCollateralAmount() &&
            tx.vout[1].nTokenId == DCT_ID{0},
            "malformed tx vouts (wrong creation fee or collateral amount)");

    return Res::Ok();
}

ResVal<CScript> CTokensConsensus::MintableToken(DCT_ID id,
                                                const CTokenImplementation &token,
                                                bool anybodyCanMint) const {
    if (token.destructionTx != uint256{}) {
        return Res::Err("token %s already destroyed at height %i by tx %s",
                        token.symbol,
                        token.destructionHeight,
                        token.destructionTx.GetHex());
    }
    const Coin &auth = coins.AccessCoin(COutPoint(token.creationTx, 1));  // always n=1 output

    // pre-bayfront logic:
    if (static_cast<int>(height) < consensus.BayfrontHeight) {
        if (id < CTokensView::DCT_ID_START) {
            return Res::Err("token %s is a 'stable coin', can't mint stable coin!", id.ToString());
        }

        if (!HasAuth(auth.out.scriptPubKey)) {
            return Res::Err("tx must have at least one input from token owner");
        }
        return {auth.out.scriptPubKey, Res::Ok()};
    }

    if (id == DCT_ID{0}) {
        return Res::Err("can't mint default DFI coin!");
    }

    if (token.IsPoolShare()) {
        return Res::Err("can't mint LPS token %s!", id.ToString());
    }

    static const auto isMainNet = Params().NetworkIDString() == CBaseChainParams::MAIN;
    // may be different logic with LPS, so, dedicated check:
    if (!token.IsMintable() || (isMainNet && mnview.GetLoanTokenByID(id))) {
        return Res::Err("token %s is not mintable!", id.ToString());
    }

    ResVal<CScript> result = {auth.out.scriptPubKey, Res::Ok()};
    if (anybodyCanMint || HasAuth(auth.out.scriptPubKey))
        return result;

    // Historic: in the case of DAT, it's ok to do not check foundation auth cause exact DAT owner is foundation
    // member himself The above is no longer true.

    if (token.IsDAT()) {
        // Is a DAT, check founders auth
        if (height < static_cast<uint32_t>(consensus.GrandCentralHeight) && !HasFoundationAuth()) {
            return Res::Err("token is DAT and tx not from foundation member");
        }
    } else {
        return Res::Err("tx must have at least one input from token owner");
    }

    return result;
}


Res CTokensConsensus::operator()(const CCreateTokenMessage &obj) const {
    auto res = CheckTokenCreationTx();
    if (!res) {
        return res;
    }

    CTokenImplementation token;
    static_cast<CToken &>(token) = obj;

    token.symbol         = trim_ws(token.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    token.name           = trim_ws(token.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);
    token.creationTx     = tx.GetHash();
    token.creationHeight = height;

    // check foundation auth
    if (token.IsDAT() && !HasFoundationAuth()) {
        return Res::Err("tx not from foundation member");
    }

    if (static_cast<int>(height) >= consensus.BayfrontHeight) {  // formal compatibility if someone cheat and create
        // LPS token on the pre-bayfront node
        if (token.IsPoolShare()) {
            return Res::Err("Cant't manually create 'Liquidity Pool Share' token; use poolpair creation");
        }
    }

    return mnview.CreateToken(token, static_cast<int>(height) < consensus.BayfrontHeight);
}

Res CTokensConsensus::operator()(const CUpdateTokenPreAMKMessage &obj) const {
    auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
    if (!pair) {
        return Res::Err("token with creationTx %s does not exist", obj.tokenTx.ToString());
    }
    auto token = pair->second;

    // check foundation auth
    auto res = HasFoundationAuth();

    if (token.IsDAT() != obj.isDAT && pair->first >= CTokensView::DCT_ID_START) {
        token.flags ^= (uint8_t)CToken::TokenFlags::DAT;
        return !res ? res : mnview.UpdateToken(token, true);
    }
    return res;
}

Res CTokensConsensus::operator()(const CUpdateTokenMessage &obj) const {
    auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
    Require(pair, "token with creationTx %s does not exist", obj.tokenTx.ToString());
    Require(pair->first != DCT_ID{0}, "Can't alter DFI token!");

    Require(!mnview.AreTokensLocked({pair->first.v}), "Cannot update token during lock");

    const auto &token = pair->second;

    // need to check it exectly here cause lps has no collateral auth (that checked next)
    Require(!token.IsPoolShare(),
            "token %s is the LPS token! Can't alter pool share's tokens!",
            obj.tokenTx.ToString());

    // check auth, depends from token's "origins"
    const Coin &auth = coins.AccessCoin(COutPoint(token.creationTx, 1));  // always n=1 output

    const auto attributes = mnview.GetAttributes();
    assert(attributes);
    std::set<CScript> databaseMembers;
    if (attributes->GetValue(CDataStructureV0{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::GovFoundation},
                             false)) {
        databaseMembers = attributes->GetValue(
                CDataStructureV0{AttributeTypes::Param, ParamIDs::Foundation, DFIPKeys::Members}, std::set<CScript>{});
    }
    bool isFoundersToken = !databaseMembers.empty() ? databaseMembers.count(auth.out.scriptPubKey) > 0
                                                    : consensus.foundationMembers.count(auth.out.scriptPubKey) > 0;

    if (isFoundersToken)
        Require(HasFoundationAuth());
    else
        Require(HasCollateralAuth(token.creationTx));

    // Check for isDAT change in non-foundation token after set height
    if (static_cast<int>(height) >= consensus.BayfrontMarinaHeight)
        // check foundation auth
        Require(obj.token.IsDAT() == token.IsDAT() || HasFoundationAuth(),
                "can't set isDAT to true, tx not from foundation member");

    CTokenImplementation updatedToken{obj.token};
    updatedToken.creationTx        = token.creationTx;
    updatedToken.destructionTx     = token.destructionTx;
    updatedToken.destructionHeight = token.destructionHeight;
    if (static_cast<int>(height) >= consensus.FortCanningHeight)
        updatedToken.symbol = trim_ws(updatedToken.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);

    return mnview.UpdateToken(updatedToken);
}

Res CTokensConsensus::operator()(const CMintTokensMessage &obj) const {
    const auto isRegTestSimulateMainnet = gArgs.GetArg("-regtest-minttoken-simulate-mainnet", false);
    const auto fortCanningCrunchHeight  = static_cast<uint32_t>(consensus.FortCanningCrunchHeight);
    const auto grandCentralHeight       = static_cast<uint32_t>(consensus.GrandCentralHeight);

    CDataStructureV0 enabledKey{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::MintTokens};
    const auto attributes = mnview.GetAttributes();
    assert(attributes);
    const auto toAddressEnabled = attributes->GetValue(enabledKey, false);

    if (!toAddressEnabled && !obj.to.empty())
        return Res::Err("Mint tokens to address is not enabled");

    // check auth and increase balance of token's owner
    for (const auto &[tokenId, amount] : obj.balances) {
        if (Params().NetworkIDString() == CBaseChainParams::MAIN && height >= fortCanningCrunchHeight &&
            mnview.GetLoanTokenByID(tokenId)) {
            return Res::Err("Loan tokens cannot be minted");
        }

        auto token = mnview.GetToken(tokenId);
        if (!token)
            return Res::Err("token %s does not exist!", tokenId.ToString());

        bool anybodyCanMint = IsRegtestNetwork() && !isRegTestSimulateMainnet;
        auto mintable       = MintableToken(tokenId, *token, anybodyCanMint);

        auto mintTokensInternal = [&](DCT_ID tokenId, CAmount amount) {
            auto minted = mnview.AddMintedTokens(tokenId, amount);
            if (!minted)
                return minted;

            CScript mintTo{*mintable.val};
            if (!obj.to.empty()) {
                CTxDestination destination;
                if (ExtractDestination(obj.to, destination) && IsValidDestination(destination))
                    mintTo = obj.to;
                else
                    return Res::Err("Invalid \'to\' address provided");
            }

            CalculateOwnerRewards(mintTo);
            auto res = mnview.AddBalance(mintTo, CTokenAmount{tokenId, amount});
            if (!res)
                return res;

            return Res::Ok();
        };

        if (!mintable)
            return std::move(mintable);

        if (anybodyCanMint || height < grandCentralHeight || !token->IsDAT() || HasFoundationAuth()) {
            auto res = mintTokensInternal(tokenId, amount);
            if (!res)
                return res;
            continue;
        }

        auto attributes = mnview.GetAttributes();
        assert(attributes);

        CDataStructureV0 enableKey{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::ConsortiumEnabled};
        CDataStructureV0 membersKey{AttributeTypes::Consortium, tokenId.v, ConsortiumKeys::MemberValues};
        const auto members = attributes->GetValue(membersKey, CConsortiumMembers{});

        if (!attributes->GetValue(enableKey, false) || members.empty()) {
            const Coin &auth = coins.AccessCoin(COutPoint(token->creationTx, 1));  // always n=1 output
            if (!HasAuth(auth.out.scriptPubKey))
                return Res::Err("You are not a foundation member or token owner and cannot mint this token!");

            auto res = mintTokensInternal(tokenId, amount);
            if (!res)
                return res;
            continue;
        }

        mintable.ok = false;

        CDataStructureV0 membersMintedKey{
                AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMembersMinted};
        auto membersBalances = attributes->GetValue(membersMintedKey, CConsortiumMembersMinted{});

        const auto dailyInterval = height / consensus.blocksPerDay() * consensus.blocksPerDay();

        for (const auto &[key, member] : members) {
            if (HasAuth(member.ownerAddress)) {
                if (member.status != CConsortiumMember::Status::Active)
                    return Res::Err("Cannot mint token, not an active member of consortium for %s!", token->symbol);

                auto add = SafeAdd(membersBalances[tokenId][key].minted, amount);
                if (!add)
                    return (std::move(add));
                membersBalances[tokenId][key].minted = add;

                if (dailyInterval == membersBalances[tokenId][key].dailyMinted.first) {
                    add = SafeAdd(membersBalances[tokenId][key].dailyMinted.second, amount);
                    if (!add)
                        return (std::move(add));
                    membersBalances[tokenId][key].dailyMinted.second = add;
                } else {
                    membersBalances[tokenId][key].dailyMinted.first  = dailyInterval;
                    membersBalances[tokenId][key].dailyMinted.second = amount;
                }

                if (membersBalances[tokenId][key].minted > member.mintLimit)
                    return Res::Err("You will exceed your maximum mint limit for %s token by minting this amount!",
                                    token->symbol);

                if (membersBalances[tokenId][key].dailyMinted.second > member.dailyMintLimit) {
                    return Res::Err("You will exceed your daily mint limit for %s token by minting this amount",
                                    token->symbol);
                }

                *mintable.val = member.ownerAddress;
                mintable.ok   = true;
                break;
            }
        }

        if (!mintable)
            return Res::Err("You are not a foundation or consortium member and cannot mint this token!");

        CDataStructureV0 maxLimitKey{AttributeTypes::Consortium, tokenId.v, ConsortiumKeys::MintLimit};
        const auto maxLimit = attributes->GetValue(maxLimitKey, CAmount{0});

        CDataStructureV0 dailyLimitKey{AttributeTypes::Consortium, tokenId.v, ConsortiumKeys::DailyMintLimit};
        const auto dailyLimit = attributes->GetValue(dailyLimitKey, CAmount{0});

        CDataStructureV0 consortiumMintedKey{
                AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMinted};
        auto globalBalances = attributes->GetValue(consortiumMintedKey, CConsortiumGlobalMinted{});

        auto add = SafeAdd(globalBalances[tokenId].minted, amount);
        if (!add)
            return (std::move(add));

        globalBalances[tokenId].minted = add;

        if (maxLimit != -1 * COIN && globalBalances[tokenId].minted > maxLimit)
            return Res::Err(
                    "You will exceed global maximum consortium mint limit for %s token by minting this amount!",
                    token->symbol);

        CAmount totalDaily{};
        for (const auto &[key, value] : membersBalances[tokenId]) {
            if (value.dailyMinted.first == dailyInterval) {
                totalDaily += value.dailyMinted.second;
            }
        }

        if (dailyLimit != -1 * COIN && totalDaily > dailyLimit)
            return Res::Err(
                    "You will exceed global daily maximum consortium mint limit for %s token by minting this "
                    "amount.",
                    token->symbol);

        attributes->SetValue(consortiumMintedKey, globalBalances);
        attributes->SetValue(membersMintedKey, membersBalances);

        auto saved = mnview.SetVariable(*attributes);
        if (!saved)
            return saved;

        auto minted = mintTokensInternal(tokenId, amount);
        if (!minted)
            return minted;
    }

    return Res::Ok();
}

Res CTokensConsensus::operator()(const CBurnTokensMessage &obj) const {
    if (obj.amounts.balances.empty()) {
        return Res::Err("tx must have balances to burn");
    }

    for (const auto &[tokenId, amount] : obj.amounts.balances) {
        // check auth
        if (!HasAuth(obj.from))
            return Res::Err("tx must have at least one input from account owner");

        if (obj.burnType != CBurnTokensMessage::BurnType::TokenBurn)
            return Res::Err("Currently only burn type 0 - TokenBurn is supported!");

        CScript ownerAddress;

        if (auto address = std::get_if<CScript>(&obj.context); address && !address->empty())
            ownerAddress = *address;
        else
            ownerAddress = obj.from;

        auto attributes = mnview.GetAttributes();
        Require(attributes, "Cannot read from attributes gov variable!");

        CDataStructureV0 membersKey{AttributeTypes::Consortium, tokenId.v, ConsortiumKeys::MemberValues};
        const auto members = attributes->GetValue(membersKey, CConsortiumMembers{});
        CDataStructureV0 membersMintedKey{
                AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMembersMinted};
        auto membersBalances = attributes->GetValue(membersMintedKey, CConsortiumMembersMinted{});
        CDataStructureV0 consortiumMintedKey{
                AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::ConsortiumMinted};
        auto globalBalances = attributes->GetValue(consortiumMintedKey, CConsortiumGlobalMinted{});

        bool setVariable = false;
        for (const auto &tmp : members)
            if (tmp.second.ownerAddress == ownerAddress) {
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

        if (setVariable) {
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
