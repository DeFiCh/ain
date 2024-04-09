// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <dfi/tokens.h>

#include <ain_rs_exports.h>
#include <amount.h>
#include <chainparams.h>  // Params()
#include <core_io.h>
#include <dfi/evm.h>
#include <dfi/mn_checks.h>
#include <ffi/cxx.h>
#include <ffi/ffihelpers.h>
#include <primitives/transaction.h>
#include <util/strencodings.h>

#include <univalue.h>

const DCT_ID CTokensView::DCT_ID_START = DCT_ID{128};

extern const std::string CURRENCY_UNIT;

std::optional<CTokensView::CTokenImpl> CTokensView::GetToken(DCT_ID id) const {
    return ReadBy<ID, CTokenImpl>(id);
}

std::optional<CTokensView::TokenIDPair> CTokensView::GetToken(const std::string &symbolKey) const {
    DCT_ID id;
    if (ReadBy<Symbol, std::string>(symbolKey, id)) {
        return std::make_pair(id, GetToken(id));
    }

    return {};
}

std::optional<std::pair<DCT_ID, CTokensView::CTokenImpl>> CTokensView::GetTokenByCreationTx(const uint256 &txid) const {
    DCT_ID id;
    if (ReadBy<CreationTx, uint256>(txid, id)) {
        if (auto tokenImpl = ReadBy<ID, CTokenImpl>(id)) {
            return std::make_pair(id, std::move(*tokenImpl));
        }
    }

    return {};
}

void CTokensView::ForEachToken(std::function<bool(const DCT_ID &, CLazySerialize<CTokenImpl>)> callback,
                               DCT_ID const &start) {
    ForEach<ID, DCT_ID, CTokenImpl>(callback, start);
}

Res CTokensView::CreateDFIToken() {
    CTokenImpl token;
    token.symbol = CURRENCY_UNIT;
    token.name = "Default Defi token";
    token.creationTx = uint256();
    token.creationHeight = 0;
    token.flags = '\0';
    token.flags |= (uint8_t)CToken::TokenFlags::DAT;
    token.flags |= (uint8_t)CToken::TokenFlags::Tradeable;
    token.flags |= (uint8_t)CToken::TokenFlags::Finalized;

    DCT_ID id{0};
    WriteBy<ID>(id, token);
    WriteBy<Symbol>(token.symbol, id);
    WriteBy<CreationTx>(token.creationTx, id);
    return Res::Ok();
}

ResVal<DCT_ID> CTokensView::CreateToken(const CTokensView::CTokenImpl &token,
                                        BlockContext &blockCtx,
                                        bool isPreBayfront) {
    if (GetTokenByCreationTx(token.creationTx)) {
        return Res::Err("token with creation tx %s already exists!", token.creationTx.ToString());
    }
    if (auto r = token.IsValidSymbol(); !r) {
        return r;
    }

    DCT_ID id{0};
    if (token.IsDAT()) {
        if (GetToken(token.symbol)) {
            return Res::Err("token '%s' already exists!", token.symbol);
        }

        ForEachToken(
            [&](DCT_ID const &currentId, CLazySerialize<CTokenImplementation>) {
                if (currentId < DCT_ID_START) {
                    id.v = currentId.v + 1;
                }
                return currentId < DCT_ID_START;
            },
            id);
        if (id == DCT_ID_START) {
            if (isPreBayfront) {
                return Res::Err("Critical fault: trying to create DCT_ID same as DCT_ID_START for Foundation owner\n");
            }

            id = IncrementLastDctId();
            LogPrintf("Warning! Range <DCT_ID_START already filled. Using \"common\" id=%s for new token\n",
                      id.ToString().c_str());
        }

        const auto evmEnabled = blockCtx.GetEVMEnabledForBlock();
        const auto &evmTemplate = blockCtx.GetEVMTemplate();
        const auto &height = blockCtx.GetHeight();
        if (evmEnabled && evmTemplate) {
            CrossBoundaryResult result;
            rust::string token_name{};
            rust::string token_symbol{};
            if (height >= static_cast<uint32_t>(Params().GetConsensus().DF23Height)) {
                if (token.name.size() > CToken::POST_METACHAIN_TOKEN_NAME_BYTE_SIZE) {
                    return Res::Err("Error creating DST20 token, token name is larger than max bytes\n");
                }
                token_name = rs_try_from_utf8(result, ffi_from_string_to_slice(token.name));
                if (!result.ok) {
                    return Res::Err("Error creating DST20 token, token name not valid UTF-8\n");
                }
                token_symbol = rs_try_from_utf8(result, ffi_from_string_to_slice(token.symbol));
                if (!result.ok) {
                    return Res::Err("Error creating DST20 token, token symbol not valid UTF-8\n");
                }
            } else {
                token_name = rust::string(token.name);
                token_symbol = rust::string(token.symbol);
            }
            evm_try_unsafe_create_dst20(result,
                                        evmTemplate->GetTemplate(),
                                        token.creationTx.GetByteArray(),
                                        DST20TokenInfo{
                                            id.v,
                                            token_name,
                                            token_symbol,
                                        });
            if (!result.ok) {
                return Res::Err("Error creating DST20 token: %s", result.reason);
            }
        }
    } else {
        id = IncrementLastDctId();
    }

    const auto symbolKey = token.CreateSymbolKey(id);
    WriteBy<ID>(id, token);
    WriteBy<Symbol>(symbolKey, id);
    WriteBy<CreationTx>(token.creationTx, id);
    return {id, Res::Ok()};
}

Res CTokensView::UpdateToken(UpdateTokenContext &ctx) {
    // checkFinalised is always true before the bayfront fork.
    const auto checkFinalised = ctx.checkFinalised;
    const auto tokenSplitUpdate = ctx.tokenSplitUpdate;
    const auto checkSymbol = ctx.checkSymbol;
    auto &blockCtx = ctx.blockCtx;
    const auto &newToken = ctx.newToken;

    auto pair = GetTokenByCreationTx(newToken.creationTx);
    if (!pair) {
        return Res::Err("token with creationTx %s does not exist!", newToken.creationTx.ToString());
    }

    auto &[id, oldToken] = *pair;

    if (checkFinalised) {
        // for compatibility, in potential case when someone cheat and create finalized token with old node (and then
        // alter dat for ex.)
        if (oldToken.IsFinalized()) {
            return Res::Err("can't alter 'Finalized' tokens");
        }
    }

    // check new symbol correctness
    if (checkSymbol) {
        if (auto res = newToken.IsValidSymbol(); !res) {
            return res;
        }
    }

    // deal with DB symbol indexes before touching symbols/DATs:
    if (oldToken.symbol != newToken.symbol ||
        oldToken.IsDAT() != newToken.IsDAT()) {  // in both cases it leads to index changes
        // create keys with regard of new flag
        std::string oldSymbolKey = oldToken.CreateSymbolKey(id);
        std::string newSymbolKey = newToken.CreateSymbolKey(id);
        if (GetToken(newSymbolKey)) {
            return Res::Err("token with key '%s' already exists!", newSymbolKey);
        }

        EraseBy<Symbol>(oldSymbolKey);
        WriteBy<Symbol>(newSymbolKey, id);
    }

    const auto height = blockCtx.GetHeight();
    const auto &consensus = blockCtx.GetConsensus();
    if (height >= consensus.DF23Height && oldToken.IsDAT() &&
        (oldToken.symbol != newToken.symbol || oldToken.name != newToken.name)) {
        const auto evmEnabled = blockCtx.GetEVMEnabledForBlock();
        const auto &evmTemplate = blockCtx.GetEVMTemplate();

        if (evmEnabled && evmTemplate) {
            const auto &hash = ctx.hash;
            CrossBoundaryResult result;
            if (newToken.name.size() > CToken::POST_METACHAIN_TOKEN_NAME_BYTE_SIZE) {
                return Res::Err("Error updating DST20 token, token name is larger than max bytes\n");
            }
            const auto token_name = rs_try_from_utf8(result, ffi_from_string_to_slice(newToken.name));
            if (!result.ok) {
                return Res::Err("Error updating DST20 token, token name not valid UTF-8\n");
            }
            const auto token_symbol = rs_try_from_utf8(result, ffi_from_string_to_slice(newToken.symbol));
            if (!result.ok) {
                return Res::Err("Error updating DST20 token, token symbol not valid UTF-8\n");
            }
            evm_try_unsafe_rename_dst20(result,
                                        evmTemplate->GetTemplate(),
                                        hash.GetByteArray(),  // Can be either TX or block hash depending on the source
                                        DST20TokenInfo{
                                            id.v,
                                            token_name,
                                            token_symbol,
                                        });
            if (!result.ok) {
                return Res::Err("Error updating DST20 token: %s", result.reason);
            }
        }
    }

    // 'name' and 'symbol' were trimmed in 'Apply'
    oldToken.name = newToken.name;
    oldToken.symbol = newToken.symbol;

    if (oldToken.IsDAT() != newToken.IsDAT()) {
        oldToken.flags ^= (uint8_t)CToken::TokenFlags::DAT;
    }

    // regular flags:
    if (oldToken.IsMintable() != newToken.IsMintable()) {
        oldToken.flags ^= (uint8_t)CToken::TokenFlags::Mintable;
    }

    if (oldToken.IsTradeable() != newToken.IsTradeable()) {
        oldToken.flags ^= (uint8_t)CToken::TokenFlags::Tradeable;
    }

    if (!oldToken.IsFinalized() && newToken.IsFinalized()) {  // IsFinalized() itself was checked upthere (with Err)
        oldToken.flags |= (uint8_t)CToken::TokenFlags::Finalized;
    }

    if (tokenSplitUpdate && oldToken.IsLoanToken() != newToken.IsLoanToken()) {
        oldToken.flags ^= (uint8_t)CToken::TokenFlags::LoanToken;
    }

    if (oldToken.destructionHeight != newToken.destructionHeight) {
        oldToken.destructionHeight = newToken.destructionHeight;
    }

    if (oldToken.destructionTx != newToken.destructionTx) {
        oldToken.destructionTx = newToken.destructionTx;
    }

    WriteBy<ID>(id, oldToken);
    return Res::Ok();
}

/*
 * Removes `Finalized` and/or `LPS` flags _possibly_set_ by bytecoded (cheated) txs before bayfront fork
 * Call this EXECTLY at the 'bayfrontHeight-1' block
 */
Res CTokensView::BayfrontFlagsCleanup() {
    ForEachToken(
        [&](DCT_ID const &id, CTokenImpl token) {
            bool changed{false};
            if (token.IsFinalized()) {
                token.flags ^= (uint8_t)CToken::TokenFlags::Finalized;
                LogPrintf("Warning! Got `Finalized` token, id=%s\n", id.ToString().c_str());
                changed = true;
            }
            if (token.IsPoolShare()) {
                token.flags ^= (uint8_t)CToken::TokenFlags::LPS;
                LogPrintf("Warning! Got `LPS` token, id=%s\n", id.ToString().c_str());
                changed = true;
            }
            if (changed) {
                WriteBy<ID>(id, token);
            }

            return true;
        },
        DCT_ID{1});  // start from non-DFI
    return Res::Ok();
}

Res CTokensView::AddMintedTokens(DCT_ID const &id, const CAmount &amount) {
    auto tokenImpl = GetToken(id);
    if (!tokenImpl) {
        return Res::Err("token with id %d does not exist!", id.v);
    }

    auto resMinted = SafeAdd(tokenImpl->minted, amount);
    if (!resMinted) {
        return Res::Err("overflow when adding to minted");
    }

    tokenImpl->minted = resMinted;

    WriteBy<ID>(id, *tokenImpl);
    return Res::Ok();
}

Res CTokensView::SubMintedTokens(DCT_ID const &id, const CAmount &amount) {
    auto tokenImpl = GetToken(id);
    if (!tokenImpl) {
        return Res::Err("token with id %d does not exist!", id.v);
    }

    auto resMinted = tokenImpl->minted - amount;
    if (resMinted < 0) {
        return Res::Err("not enough tokens exist to subtract this amount");
    }

    tokenImpl->minted = resMinted;

    WriteBy<ID>(id, *tokenImpl);
    return Res::Ok();
}

DCT_ID CTokensView::IncrementLastDctId() {
    DCT_ID result{DCT_ID_START};
    auto lastDctId = ReadLastDctId();
    if (lastDctId) {
        result = DCT_ID{std::max(lastDctId->v + 1, result.v)};
    }
    assert(Write(LastDctId::prefix(), result));
    return result;
}

std::optional<DCT_ID> CTokensView::ReadLastDctId() const {
    DCT_ID lastDctId{DCT_ID_START};
    if (Read(LastDctId::prefix(), lastDctId)) {
        return {lastDctId};
    }
    return {};
}

inline Res CTokenImplementation::IsValidSymbol() const {
    auto invalidTokenSymbol = []() {
        return Res::Err("Invalid token symbol. Valid: Start with an alphabet, non-empty, not contain # or /");
    };

    if (symbol.empty() || IsDigit(symbol[0])) {
        return invalidTokenSymbol();
    }
    if (symbol.find('#') != std::string::npos) {
        return invalidTokenSymbol();
    }
    if (creationHeight >= Params().GetConsensus().DF16FortCanningCrunchHeight) {
        if (symbol.find('/') != std::string::npos) {
            return invalidTokenSymbol();
        };
    }
    return Res::Ok();
}

void CTokensView::SetTokenSplitMultiplier(const uint32_t oldId,
                                          const uint32_t newId,
                                          const SplitMultiplier multiplier) {
    WriteBy<TokenSplitMultiplier>(oldId, std::make_pair(newId, multiplier));
}

std::optional<std::pair<uint32_t, CTokensView::SplitMultiplier>> CTokensView::GetTokenSplitMultiplier(
    const uint32_t id) const {
    std::pair<uint32_t, SplitMultiplier> idMultiplierPair;
    if (ReadBy<TokenSplitMultiplier, uint32_t>(id, idMultiplierPair)) {
        return idMultiplierPair;
    }

    return {};
}
