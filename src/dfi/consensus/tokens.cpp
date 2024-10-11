// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <coins.h>
#include <consensus/params.h>
#include <dfi/consensus/tokens.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/masternodes.h>
#include <dfi/mn_checks.h>
#include <cstdint>

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

    // Foundation auth no longer used after DF24Height
    if (!token.IsDAT() || height >= static_cast<uint32_t>(consensus.DF24Height)) {
        return Res::Err("tx must have at least one input from token owner");
    }

    // It is a DAT, check founders auth
    if (!HasFoundationAuth()) {
        return Res::Err("token is DAT and tx not from foundation member");
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

    auto authCheck = AuthManager(blockCtx, txCtx);
    if (token.IsDAT() && !authCheck.HasGovOrFoundationAuth()) {
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

    // need to check it exectly here cause lps has no collateral auth (that checked next)
    if (token.IsPoolShare()) {
        return Res::Err("token %s is the LPS token! Can't alter pool share's tokens!", obj.tokenTx.ToString());
    }

    if (height >= Params().GetConsensus().DF24Height) {
        CTokenImplementation updatedToken{obj.token};
        updatedToken.creationTx = token.creationTx;
        updatedToken.destructionTx = token.destructionTx;
        updatedToken.destructionHeight = token.destructionHeight;
        updatedToken.creationHeight = token.creationHeight;
        updatedToken.symbol = trim_ws(updatedToken.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);

        // Check for isDAT change
        if (updatedToken.IsDAT() != token.IsDAT()) {
            // We disallow this for now since we don't yet support dynamic migration
            // of non DAT to EVM if it's suddenly turned into a DAT.
            return Res::Err("Cannot change isDAT flag after DF23Height");
        }

        auto authCheck = AuthManager(blockCtx, txCtx);
        const auto newCollateralTx = mnview.GetNewTokenCollateralTXID(tokenID.v);
        Res ownerAuth = HasCollateralAuth(newCollateralTx == uint256{} ? token.creationTx : newCollateralTx);

        if (!ownerAuth) {
            // Governance or foundation can still mark/unmark token deprecation
            if (auto res = authCheck.HasGovOrFoundationAuth(); !res) {
                return Res::Err("Authentication failed for token owner");
            }

            // If it's loan token, that's owned by gov, so we don't need to disallow.
            // Limit update token for governance and foundation for non-loan tokens
            if (!mnview.GetLoanTokenByID(tokenID)) {
                // Allow only deprecation. We disallow changes like name or symbol
                // as a token holder shouldn't be misrepresented by governance.
                // Governance can choose completely discard it by deprecating it
                // or keep it in the form as intended by the owner.

                const auto toggledFlags = static_cast<uint8_t>(updatedToken.flags ^ token.flags);
                const auto hasDisallowedFlagToggle =
                    toggledFlags != static_cast<uint8_t>(CToken::TokenFlags::Deprecated);

                const auto disallowedChanges = hasDisallowedFlagToggle || updatedToken.symbol != token.symbol ||
                                               updatedToken.name != token.name || obj.newCollateralAddress;

                if (disallowedChanges) {
                    return Res::Err("Only token deprecation toggle is allowed by governance");
                }
            }
        }

        if (obj.newCollateralAddress) {
            if (auto res = CheckTokenCreationTx(false); !res) {
                return res;
            }
            mnview.EraseNewTokenCollateral(tokenID.v);
            mnview.SetNewTokenCollateral(hash, tokenID.v);
        }

        UpdateTokenContext ctx{updatedToken, blockCtx, true, false, true, hash};
        return mnview.UpdateToken(ctx);
    } else {
        if (obj.newCollateralAddress) {
            return Res::Err("Collateral address update is not allowed before DF24Height");
        }
        const auto deprecationMask = static_cast<uint8_t>(CToken::TokenFlags::Deprecated);
        if ((obj.token.flags & deprecationMask) == deprecationMask) {
            return Res::Err("Token cannot be deprecated below DF24Height");
        }
        // Old code path below, untouched

        // check auth, depends from token's "origins"
        const Coin &auth = coins.AccessCoin(COutPoint(token.creationTx, 1));  // always n=1 output

        const auto attributes = mnview.GetAttributes();
        std::set<CScript> databaseMembers;
        if (attributes->GetValue(CDataStructureV0{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::GovFoundation},
                                 false)) {
            databaseMembers = attributes->GetValue(
                CDataStructureV0{AttributeTypes::Param, ParamIDs::Foundation, DFIPKeys::Members}, std::set<CScript>{});
        }
        bool isFoundersToken = !databaseMembers.empty() ? databaseMembers.count(auth.out.scriptPubKey) > 0
                                                        : consensus.foundationMembers.count(auth.out.scriptPubKey) > 0;

        if (isFoundersToken) {
            if (auto res = HasFoundationAuth(); !res) {
                return res;
            }
        } else {
            if (auto res = HasCollateralAuth(token.creationTx); !res) {
                return res;
            }
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

        CTokenImplementation updatedToken{obj.token};
        updatedToken.creationTx = token.creationTx;
        updatedToken.destructionTx = token.destructionTx;
        updatedToken.destructionHeight = token.destructionHeight;
        if (static_cast<int>(height) >= consensus.DF11FortCanningHeight) {
            updatedToken.symbol = trim_ws(updatedToken.symbol).substr(0, CToken::MAX_TOKEN_SYMBOL_LENGTH);
        }

        const auto checkSymbol = height >= static_cast<uint32_t>(consensus.DF23Height);
        UpdateTokenContext ctx{updatedToken, blockCtx, true, false, checkSymbol, hash};
        return mnview.UpdateToken(ctx);
    }
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
