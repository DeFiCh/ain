// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/tokens.h>

#include <amount.h>
#include <core_io.h>
#include <primitives/transaction.h>

#include <univalue.h>

const DCT_ID CTokensView::DCT_ID_START = DCT_ID{128};

extern const std::string CURRENCY_UNIT;

std::string trim_ws(std::string const & str)
{
    std::string const ws = " \n\r\t";
    size_t first = str.find_first_not_of(ws);
    if (std::string::npos == first)
    {
        return str;
    }
    size_t last = str.find_last_not_of(ws);
    return str.substr(first, (last - first + 1));
}

std::unique_ptr<CToken> CTokensView::GetToken(DCT_ID id) const
{
    if (auto tokenImpl = ReadBy<ID, CTokenImpl>(id)) {
        return std::make_unique<CTokenImpl>(*tokenImpl);
    }

    return {};
}

std::optional<std::pair<DCT_ID, std::unique_ptr<CToken> > > CTokensView::GetToken(const std::string & symbolKey) const
{
    DCT_ID id;
    if (ReadBy<Symbol, std::string>(symbolKey, id)) {
        return std::make_pair(id, GetToken(id));
    }
    return {};
}

std::optional<std::pair<DCT_ID, CTokensView::CTokenImpl> > CTokensView::GetTokenByCreationTx(const uint256 & txid) const
{
    DCT_ID id;
    if (ReadBy<CreationTx, uint256>(txid, id)) {
        if (auto tokenImpl = ReadBy<ID, CTokenImpl>(id)) {
            return std::make_pair(id, std::move(*tokenImpl));
        }
    }
    return {};
}

std::unique_ptr<CToken> CTokensView::GetTokenGuessId(const std::string & str, DCT_ID & id) const
{
    std::string const key = trim_ws(str);

    if (key.empty()) {
        id = DCT_ID{0};
        return GetToken(id);
    }
    if (ParseUInt32(key, &id.v))
        return GetToken(id);

    uint256 tx;
    if (ParseHashStr(key, tx)) {
        auto pair = GetTokenByCreationTx(tx);
        if (pair) {
            id = pair->first;
            return std::make_unique<CTokenImpl>(pair->second);
        }
    } else {
        auto pair = GetToken(key);
        if (pair) {
            id = pair->first;
            return std::move(pair->second);
        }
    }
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
    if (GetTokenByCreationTx(token.creationTx)) {
        return Res::Err("token with creation tx %s already exists!", token.creationTx.ToString());
    }

    auto checkSymbolRes = token.IsValidSymbol();
    if (!checkSymbolRes.ok) {
        return checkSymbolRes;
    }

    DCT_ID id{0};
    if(token.IsDAT()) {
        if (GetToken(token.symbol)) {
            return Res::Err("token '%s' already exists!", token.symbol);
        }
        ForEachToken([&](DCT_ID const& currentId, CLazySerialize<CTokenImplementation>) {
            if(currentId < DCT_ID_START)
                id.v = currentId.v + 1;
            return currentId < DCT_ID_START;
        }, id);
        if (id == DCT_ID_START) {
            if (isPreBayfront)
                return Res::Err("Critical fault: trying to create DCT_ID same as DCT_ID_START for Foundation owner\n"); // asserted before BayfrontHeight, keep it for strict sameness
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

Res CTokensView::RevertCreateToken(const uint256 & txid)
{
    auto pair = GetTokenByCreationTx(txid);
    if (!pair) {
        return Res::Err("Token creation revert error: token with creation tx %s does not exist!\n", txid.ToString());
    }
    DCT_ID id = pair->first;
    auto lastId = ReadLastDctId();
    if (!lastId || (*lastId) != id) {
        return Res::Err("Token creation revert error: revert sequence broken! (txid = %s, id = %s, LastDctId = %s)\n", txid.ToString(), id.ToString(), (lastId ? lastId->ToString() : DCT_ID{0}.ToString()));
    }
    auto const & token = pair->second;
    EraseBy<ID>(id);
    EraseBy<Symbol>(token.symbol);
    EraseBy<CreationTx>(token.creationTx);
    DecrementLastDctId();
    return Res::Ok();
}

Res CTokensView::UpdateToken(const uint256 &tokenTx, const CToken& newToken, bool isPreBayfront)
{
    auto pair = GetTokenByCreationTx(tokenTx);
    if (!pair) {
        return Res::Err("token with creationTx %s does not exist!", tokenTx.ToString());
    }
    DCT_ID id = pair->first;
    CTokenImpl & oldToken = pair->second;

    if (!isPreBayfront && oldToken.IsFinalized()) { // for compatibility, in potential case when someone cheat and create finalized token with old node (and then alter dat for ex.)
        return Res::Err("can't alter 'Finalized' tokens");
    }

    // 'name' and 'symbol' were trimmed in 'Apply'
    oldToken.name = newToken.name;

    // check new symbol correctness
    auto checkSymbolRes = newToken.IsValidSymbol();
    if (!checkSymbolRes.ok) {
        return checkSymbolRes;
    }

    // deal with DB symbol indexes before touching symbols/DATs:
    if (oldToken.symbol != newToken.symbol || oldToken.IsDAT() != newToken.IsDAT()) { // in both cases it leads to index changes
        // create keys with regard of new flag
        std::string oldSymbolKey = oldToken.CreateSymbolKey(id);
        std::string newSymbolKey = newToken.CreateSymbolKey(id);
        if (GetToken(newSymbolKey)) {
            return Res::Err("token with key '%s' already exists!", newSymbolKey);
        }
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
        if (changed) {
            WriteBy<ID>(id, token);
        }
        return true;
    }, DCT_ID{1}); // start from non-DFI
    return Res::Ok();
}

Res CTokensView::AddMintedTokens(const uint256 &tokenTx, CAmount const & amount)
{
    auto pair = GetTokenByCreationTx(tokenTx);
    if (!pair) {
        return Res::Err("token with creationTx %s does not exist!", tokenTx.ToString());
    }
    CTokenImpl & tokenImpl = pair->second;

    auto resMinted = SafeAdd(tokenImpl.minted, amount);
    if (!resMinted.ok) {
        return Res::Err("overflow when adding to minted");
    }
    tokenImpl.minted = *resMinted.val;

    WriteBy<ID>(pair->first, tokenImpl);
    return Res::Ok();
}

Res CTokensView::SubMintedTokens(const uint256 &tokenTx, CAmount const & amount)
{
    auto pair = GetTokenByCreationTx(tokenTx);
    if (!pair) {
        return Res::Err("token with creationTx %s does not exist!", tokenTx.ToString());
    }
    CTokenImpl & tokenImpl = pair->second;

    auto resMinted = tokenImpl.minted - amount;
    if (resMinted < 0) {
        return Res::Err("not enough tokens exist to subtract this amount");
    }
    tokenImpl.minted = resMinted;

    WriteBy<ID>(pair->first, tokenImpl);
    return Res::Ok();
}

DCT_ID CTokensView::IncrementLastDctId()
{
    DCT_ID result{DCT_ID_START};
    auto lastDctId = ReadLastDctId();
    if (lastDctId) {
        result = DCT_ID{std::max(lastDctId->v + 1, result.v)};
    }
    assert (Write(LastDctId::prefix(), result));
    return result;
}

DCT_ID CTokensView::DecrementLastDctId()
{
    auto lastDctId = ReadLastDctId();
    if (lastDctId && *lastDctId >= DCT_ID_START) {
        --(lastDctId->v);
    } else {
        LogPrintf("Critical fault: trying to decrement nonexistent DCT_ID or it is lower than DCT_ID_START\n");
        assert (false);
    }
    assert (Write(LastDctId::prefix(), *lastDctId)); // it is ok if (DCT_ID_START - 1) will be written
    return *lastDctId;
}

std::optional<DCT_ID> CTokensView::ReadLastDctId() const
{
    DCT_ID lastDctId{DCT_ID_START};
    if (Read(LastDctId::prefix(), lastDctId)) {
        return {lastDctId};
    }
    return {};
}
