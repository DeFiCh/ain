// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <coins.h>
#include <consensus/params.h>
#include <dfi/consensus/tokens.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/masternodes.h>
#include <dfi/mn_checks.h>

Res CTokensConsensus::CheckTokenCreationTx(const bool creation) const {
    const auto height = txCtx.GetHeight();
    const auto &tx = txCtx.GetTransaction();

    if (tx.vout.size() < 2 || (creation && tx.vout[0].nValue < GetTokenCreationFee(height)) ||
        tx.vout[0].nTokenId != DCT_ID{0} || tx.vout[1].nValue != GetTokenCollateralAmount() ||
        tx.vout[1].nTokenId != DCT_ID{0}) {
        return Res::Err("malformed tx vouts (wrong creation fee or collateral amount)");
    }

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

    const auto &coins = txCtx.GetCoins();
    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto &mnview = blockCtx.GetView();

    Coin auth;
    if (const auto txid = mnview.GetNewTokenCollateralTXID(id.v); txid != uint256{}) {
        auth = coins.AccessCoin(COutPoint(txid, 1));
    } else {
        auth = coins.AccessCoin(COutPoint(token.creationTx, 1));
    }

    // pre-bayfront logic:
    if (static_cast<int>(height) < consensus.DF2BayfrontHeight) {
        if (id < CTokensView::DCT_ID_START) {
            return Res::Err("token %s is a 'stable coin', can't mint stable coin!", id.ToString());
        }

        if (!HasAuth(auth.out.scriptPubKey)) {
            return Res::Err("tx must have at least one input from token owner");
        }
        return {auth.out.scriptPubKey, Res::Ok()};
    }

    if (id == DCT_ID{0}) {
        if (IsRegtestNetwork()) {
            ResVal<CScript> result{auth.out.scriptPubKey, Res::Ok()};
            return result;
        }
        return Res::Err("can't mint default DFI coin!");
    }

    if (token.IsPoolShare()) {
        return Res::Err("can't mint LPS token %s!", id.ToString());
    }

    static const auto isMainNet = Params().NetworkIDString() == CBaseChainParams::MAIN;
    // may be different logic with LPS, so, dedicated check:

    if (!token.IsMintable() || (isMainNet && !fMockNetwork && mnview.GetLoanTokenByID(id))) {
        return Res::Err("token %s is not mintable!", id.ToString());
    }

    ResVal<CScript> result{auth.out.scriptPubKey, Res::Ok()};
    if (anybodyCanMint || HasAuth(auth.out.scriptPubKey)) {
        return result;
    }

    if (!token.IsDAT()) {
        return Res::Err("tx must have at least one input from token owner");
    }

    // It is a DAT, check founders auth
    if (!HasFoundationAuth()) {
        return Res::Err("token is DAT and tx not from foundation member");
    }

    // Foundation auth no longer used after DF24Height
    if (height >= static_cast<uint32_t>(consensus.DF24Height)) {
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

    auto tokenSymbol = trim_ws(token.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    auto tokenName = trim_ws(token.name).substr(0, CToken::MAX_TOKEN_NAME_LENGTH);

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto &tx = txCtx.GetTransaction();
    auto &mnview = blockCtx.GetView();

    token.symbol = tokenSymbol;
    token.name = tokenName;
    token.creationTx = tx.GetHash();
    token.creationHeight = height;

    if (token.IsDAT() && !HasFoundationAuth() && !HasGovernanceAuth()) {
        return Res::Err("tx not from foundation member");
    }

    if (static_cast<int>(height) >= consensus.DF2BayfrontHeight) {
        if (token.IsPoolShare()) {
            return Res::Err("Can't manually create 'Liquidity Pool Share' token; use poolpair creation");
        }
    }

    const auto isPreBayFront = static_cast<int>(height) < consensus.DF2BayfrontHeight;
    auto tokenId = mnview.CreateToken(token, blockCtx, isPreBayFront);
    return tokenId;
}

Res CTokensConsensus::operator()(const CUpdateTokenPreAMKMessage &obj) const {
    auto &mnview = blockCtx.GetView();
    auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
    if (!pair) {
        return Res::Err("token with creationTx %s does not exist", obj.tokenTx.ToString());
    }
    auto token = pair->second;

    // check foundation auth
    auto res = HasFoundationAuth();

    if (token.IsDAT() != obj.isDAT && pair->first >= CTokensView::DCT_ID_START) {
        token.flags ^= (uint8_t)CToken::TokenFlags::DAT;
        UpdateTokenContext ctx{token,
                               blockCtx};  // CUpdateTokenPreAMKMessage disabled after Bayfront. No TX hash needed.
        return !res ? res : mnview.UpdateToken(ctx);
    }
    return res;
}

Res CTokensConsensus::operator()(const CUpdateTokenMessage &obj) const {
    const auto &coins = txCtx.GetCoins();
    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto hash = txCtx.GetTransaction().GetHash();
    auto &mnview = blockCtx.GetView();

    auto pair = mnview.GetTokenByCreationTx(obj.tokenTx);
    if (!pair) {
        return Res::Err("token with creationTx %s does not exist", obj.tokenTx.ToString());
    }

    const auto &[tokenID, token] = *pair;

    if (tokenID == DCT_ID{0}) {
        return Res::Err("Can't alter DFI token!");
    }

    if (mnview.AreTokensLocked({tokenID.v})) {
        return Res::Err("Cannot update token during lock");
    }

    if (height < Params().GetConsensus().DF24Height && obj.newCollateralAddress) {
        return Res::Err("Collateral address update is not allowed before DF24Height");
    }

    // need to check it exectly here cause lps has no collateral auth (that checked next)
    if (token.IsPoolShare()) {
        return Res::Err("token %s is the LPS token! Can't alter pool share's tokens!", obj.tokenTx.ToString());
    }

    // check auth, depends from token's "origins"
    const Coin &auth = coins.AccessCoin(COutPoint(token.creationTx, 1));  // always n=1 output

    bool isFoundersToken{};

    if (height < static_cast<uint32_t>(consensus.DF24Height)) {
        const auto members = GetFoundationMembers(mnview);
        isFoundersToken = !members.empty() ? members.count(auth.out.scriptPubKey) > 0
                                           : consensus.foundationMembers.count(auth.out.scriptPubKey) > 0;
    }

    CTokenImplementation updatedToken{obj.token};
    updatedToken.creationTx = token.creationTx;
    updatedToken.destructionTx = token.destructionTx;
    updatedToken.destructionHeight = token.destructionHeight;

    // Set creation height, otherwise invalid symbol check is skipped
    if (height >= static_cast<uint32_t>(consensus.DF24Height)) {
        updatedToken.creationHeight = token.creationHeight;
    }

    const auto foundationAuth = HasFoundationAuth();
    const auto governanceAuth = HasGovernanceAuth();

    Res ownerAuth = Res::Ok();
    if (const auto txid = mnview.GetNewTokenCollateralTXID(tokenID.v); txid != uint256{}) {
        ownerAuth = HasCollateralAuth(txid);
    } else {
        ownerAuth = HasCollateralAuth(token.creationTx);
    }

    const auto deprecatedMask = ~static_cast<uint8_t>(CToken::TokenFlags::Deprecated);
    const auto disallowedChanges =
        updatedToken.symbol != token.symbol || updatedToken.name != token.name || obj.newCollateralAddress;

    // Foundation or Governance can deprecate tokens
    if (updatedToken.IsDeprecated()) {
        if (height < static_cast<uint32_t>(consensus.DF24Height)) {
            return Res::Err("Token cannot be deprecated below DF24Height");
        }
        if (token.IsDeprecated()) {
            return Res::Err("Token already deprecated");
        }
        if (!foundationAuth && !governanceAuth && !ownerAuth) {
            return Res::Err("Token deprecation must have auth from the owner, Foundation or Governance");
        }
        if ((foundationAuth || governanceAuth) && !ownerAuth) {
            // Check no other changes are being made
            if (disallowedChanges || (updatedToken.flags & deprecatedMask) != token.flags) {
                return Res::Err("Token deprecation by Governance or Foundation must not have any other changes");
            }
        }
    } else if (isFoundersToken) {
        if (!foundationAuth) {
            return foundationAuth;
        }
    } else {
        // Allow Foundation of Governance to undeprecate tokens
        if (height >= static_cast<uint32_t>(consensus.DF24Height) && !ownerAuth && (foundationAuth || governanceAuth)) {
            if (!token.IsDeprecated()) {
                return ownerAuth;
            }
            // Check no other changes are being made
            if (disallowedChanges || updatedToken.flags != (token.flags & deprecatedMask)) {
                return Res::Err("Token undeprecation by Governance or Foundation must not have any other changes");
            }
        } else if (!ownerAuth) {
            return ownerAuth;
        }
    }

    if (obj.newCollateralAddress) {
        if (auto res = CheckTokenCreationTx(false); !res) {
            return res;
        }

        mnview.EraseNewTokenCollateral(tokenID.v);
        mnview.SetNewTokenCollateral(hash, tokenID.v);
    }

    // Check for isDAT change
    if (obj.token.IsDAT() != token.IsDAT()) {
        if (height >= static_cast<uint32_t>(consensus.DF23Height)) {
            // We disallow this for now since we don't yet support dynamic migration
            // of non DAT to EVM if it's suddenly turned into a DAT.
            return Res::Err("Cannot change isDAT flag after DF23Height");
        } else if (height >= static_cast<uint32_t>(consensus.DF3BayfrontMarinaHeight) && !HasFoundationAuth()) {
            return Res::Err("Foundation auth required to change isDAT flag");
        }
    }

    if (static_cast<int>(height) >= consensus.DF11FortCanningHeight) {
        updatedToken.symbol = trim_ws(updatedToken.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
    }

    const auto checkSymbol = height >= static_cast<uint32_t>(consensus.DF23Height);
    UpdateTokenContext ctx{updatedToken, blockCtx, true, false, checkSymbol, hash};
    return mnview.UpdateToken(ctx);
}

Res CTokensConsensus::operator()(const CMintTokensMessage &obj) const {
    auto &mnview = blockCtx.GetView();

    const auto isRegTestSimulateMainnet = gArgs.GetArg("-regtest-minttoken-simulate-mainnet", false);
    const auto anybodyCanMint = IsRegtestNetwork() && !isRegTestSimulateMainnet;

    CDataStructureV0 enabledKey{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::MintTokens};
    const auto attributes = mnview.GetAttributes();
    const auto toAddressEnabled = attributes->GetValue(enabledKey, IsRegtestNetwork() ? true : false);

    if (!toAddressEnabled && !obj.to.empty()) {
        return Res::Err("Mint tokens to address is not enabled");
    }

    // check auth and increase balance of token's owner
    for (const auto &[tokenId, amount] : obj.balances) {
        const auto token = mnview.GetToken(tokenId);
        if (!token) {
            return Res::Err("token %s does not exist!", tokenId.ToString());
        }

        auto mintable = MintableToken(tokenId, *token, anybodyCanMint);
        if (!mintable) {
            return std::move(mintable);
        }

        if (auto minted = mnview.AddMintedTokens(tokenId, amount); !minted) {
            return minted;
        }

        auto mintTo{*mintable.val};
        if (!obj.to.empty()) {
            CTxDestination destination;
            if (ExtractDestination(obj.to, destination) && IsValidDestination(destination)) {
                mintTo = obj.to;
            } else {
                return Res::Err("Invalid \'to\' address provided");
            }
        }

        CalculateOwnerRewards(mintTo);
        if (auto res = mnview.AddBalance(mintTo, CTokenAmount{tokenId, amount}); !res) {
            return res;
        }
    }

    return Res::Ok();
}

Res CTokensConsensus::operator()(const CBurnTokensMessage &obj) const {
    if (obj.amounts.balances.empty()) {
        return Res::Err("tx must have balances to burn");
    }

    const auto &consensus = txCtx.GetConsensus();

    for (const auto &[tokenId, amount] : obj.amounts.balances) {
        // check auth
        if (!HasAuth(obj.from)) {
            return Res::Err("tx must have at least one input from account owner");
        }

        if (obj.burnType != CBurnTokensMessage::BurnType::TokenBurn) {
            return Res::Err("Currently only burn type 0 - TokenBurn is supported!");
        }

        CalculateOwnerRewards(obj.from);

        auto res = TransferTokenBalance(tokenId, amount, obj.from, consensus.burnAddress);
        if (!res) {
            return res;
        }
    }

    return Res::Ok();
}
