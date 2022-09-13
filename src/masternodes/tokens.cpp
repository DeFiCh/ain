// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/tokens.h>

#include <amount.h>
#include <chainparams.h> // Params()
#include <core_io.h>
#include <primitives/transaction.h>
#include <util/strencodings.h>

#include <univalue.h>

const DCT_ID CTokensView::DCT_ID_START = DCT_ID{128};

extern const std::string CURRENCY_UNIT;

std::optional<CTokensView::CTokenImpl> CTokensView::GetToken(DCT_ID id) const
{
    return ReadBy<ID, CTokenImpl>(id);
}

std::optional<std::pair<DCT_ID, std::optional<CTokensView::CTokenImpl>>> CTokensView::GetToken(const std::string & symbolKey) const
{
    DCT_ID id;
    if (ReadBy<Symbol, std::string>(symbolKey, id))
        return std::make_pair(id, GetToken(id));

    return {};
}

std::optional<std::pair<DCT_ID, CTokensView::CTokenImpl> > CTokensView::GetTokenByCreationTx(const uint256 & txid) const
{
    DCT_ID id;
    if (ReadBy<CreationTx, uint256>(txid, id))
        if (auto tokenImpl = ReadBy<ID, CTokenImpl>(id))
            return std::make_pair(id, std::move(*tokenImpl));

    return {};
}

void CTokensView::ForEachToken(std::function<bool (const DCT_ID &, CLazySerialize<CTokenImpl>)> callback, DCT_ID const & start)
{
    ForEach<ID, DCT_ID, CTokenImpl>(callback, start);
}

Res CTokensView::CreateDFIToken()
{
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

ResVal<DCT_ID> CTokensView::CreateToken(const CTokensView::CTokenImpl & token, bool isPreBayfront)
{
    // this should not happen, but for sure
    Require(!GetTokenByCreationTx(token.creationTx), "token with creation tx %s already exists!", token.creationTx.ToString());

    Require(token.IsValidSymbol());

    DCT_ID id{0};
    if(token.IsDAT()) {
        Require(!GetToken(token.symbol), "token '%s' already exists!", token.symbol);

        ForEachToken([&](DCT_ID const& currentId, CLazySerialize<CTokenImplementation>) {
            if(currentId < DCT_ID_START)
                id.v = currentId.v + 1;
            return currentId < DCT_ID_START;
        }, id);
        if (id == DCT_ID_START) {
            Require(!isPreBayfront, "Critical fault: trying to create DCT_ID same as DCT_ID_START for Foundation owner\n"); // asserted before BayfrontHeight, keep it for strict sameness
            id = IncrementLastDctId();
            LogPrintf("Warning! Range <DCT_ID_START already filled. Using \"common\" id=%s for new token\n", id.ToString().c_str());
        }
    } else {
        id = IncrementLastDctId();
    }

    std::string symbolKey = token.CreateSymbolKey(id);

    WriteBy<ID>(id, token);
    WriteBy<Symbol>(symbolKey, id);
    WriteBy<CreationTx>(token.creationTx, id);
    return {id, Res::Ok()};
}

Res CTokensView::UpdateToken(const CTokenImpl& newToken, bool isPreBayfront, const bool tokenSplitUpdate)
{
    auto pair = GetTokenByCreationTx(newToken.creationTx);
    Require(pair, "token with creationTx %s does not exist!", newToken.creationTx.ToString());

    DCT_ID id = pair->first;
    CTokenImpl & oldToken = pair->second;

    if (!isPreBayfront)
        // for compatibility, in potential case when someone cheat and create finalized token with old node (and then alter dat for ex.)
        Require(!oldToken.IsFinalized(), "can't alter 'Finalized' tokens");

    // 'name' and 'symbol' were trimmed in 'Apply'
    oldToken.name = newToken.name;

    // check new symbol correctness
    if (!tokenSplitUpdate) {
        Require(newToken.IsValidSymbol());
    }

    // deal with DB symbol indexes before touching symbols/DATs:
    if (oldToken.symbol != newToken.symbol || oldToken.IsDAT() != newToken.IsDAT()) { // in both cases it leads to index changes
        // create keys with regard of new flag
        std::string oldSymbolKey = oldToken.CreateSymbolKey(id);
        std::string newSymbolKey = newToken.CreateSymbolKey(id);
        Require(!GetToken(newSymbolKey), "token with key '%s' already exists!", newSymbolKey);

        EraseBy<Symbol>(oldSymbolKey);
        WriteBy<Symbol>(newSymbolKey, id);
    }

    // apply DAT flag and symbol only AFTER dealing with symbol indexes:
    oldToken.symbol = newToken.symbol;
    if (oldToken.IsDAT() != newToken.IsDAT())
        oldToken.flags ^= (uint8_t)CToken::TokenFlags::DAT;

    // regular flags:
    if (oldToken.IsMintable() != newToken.IsMintable())
        oldToken.flags ^= (uint8_t)CToken::TokenFlags::Mintable;

    if (oldToken.IsTradeable() != newToken.IsTradeable())
        oldToken.flags ^= (uint8_t)CToken::TokenFlags::Tradeable;

    if (!oldToken.IsFinalized() && newToken.IsFinalized()) // IsFinalized() itself was checked upthere (with Err)
        oldToken.flags |= (uint8_t)CToken::TokenFlags::Finalized;

    if (tokenSplitUpdate && oldToken.IsLoanToken() != newToken.IsLoanToken())
        oldToken.flags ^= (uint8_t)CToken::TokenFlags::LoanToken;

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
Res CTokensView::BayfrontFlagsCleanup()
{
    ForEachToken([&] (DCT_ID const & id, CTokenImpl token){
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
        if (changed)
            WriteBy<ID>(id, token);

        return true;
    }, DCT_ID{1}); // start from non-DFI
    return Res::Ok();
}

Res CTokensView::AddMintedTokens(DCT_ID const &id, CAmount const & amount)
{
    auto tokenImpl = GetToken(id);
    Require(tokenImpl, "token with id %d does not exist!", id.v);

    auto resMinted = SafeAdd(tokenImpl->minted, amount);
    Require(resMinted, "overflow when adding to minted");

    tokenImpl->minted = resMinted;

    WriteBy<ID>(id, *tokenImpl);
    return Res::Ok();
}

Res CTokensView::SubMintedTokens(DCT_ID const &id, CAmount const & amount)
{
    auto tokenImpl = GetToken(id);
    Require(tokenImpl, "token with id %d does not exist!", id.v);

    auto resMinted = tokenImpl->minted - amount;
    Require(resMinted >= 0, "not enough tokens exist to subtract this amount");

    tokenImpl->minted = resMinted;

    WriteBy<ID>(id, *tokenImpl);
    return Res::Ok();
}

DCT_ID CTokensView::IncrementLastDctId()
{
    DCT_ID result{DCT_ID_START};
    auto lastDctId = ReadLastDctId();
    if (lastDctId)
        result = DCT_ID{std::max(lastDctId->v + 1, result.v)};

    assert (Write(LastDctId::prefix(), result));
    return result;
}

std::optional<DCT_ID> CTokensView::ReadLastDctId() const
{
    DCT_ID lastDctId{DCT_ID_START};
    if (Read(LastDctId::prefix(), lastDctId)) {
        return {lastDctId};
    }

    return {};
}

inline Res CTokenImplementation::IsValidSymbol() const
{
    Require(!symbol.empty() && !IsDigit(symbol[0]), "token symbol should be non-empty and starts with a letter");
    Require(symbol.find('#') == std::string::npos, "token symbol should not contain '#'");
    if (creationHeight >= Params().GetConsensus().FortCanningCrunchHeight) {
        Require(symbol.find('/') == std::string::npos, "token symbol should not contain '/'");
    }
    return Res::Ok();
}
