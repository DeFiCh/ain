// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/tokens.h>
#include <core_io.h>
#include <primitives/transaction.h>

/// @attention make sure that it does not overlap with other views !!!
const unsigned char CTokensView::ID          ::prefix = 'T';
const unsigned char CTokensView::Symbol      ::prefix = 'S';
const unsigned char CTokensView::CreationTx  ::prefix = 'c';
const unsigned char CTokensView::LastDctId   ::prefix = 'L';

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
    auto tokenImpl = ReadBy<ID, CTokenImpl>(WrapVarInt(id.v)); // @todo change serialization of DCT_ID to VarInt by default?
    if (tokenImpl)
        return MakeUnique<CTokenImpl>(*tokenImpl);

    return {};
}

boost::optional<std::pair<DCT_ID, std::unique_ptr<CToken> > > CTokensView::GetToken(const std::string & symbolKey) const
{
    DCT_ID id;
    auto varint = WrapVarInt(id.v);

    if (ReadBy<Symbol, std::string>(symbolKey, varint)) {
//        assert(id >= DCT_ID_START);// ? not needed anymore?
        return { std::make_pair(id, std::move(GetToken(id)))};
    }
    return {};
}

boost::optional<std::pair<DCT_ID, CTokensView::CTokenImpl> > CTokensView::GetTokenByCreationTx(const uint256 & txid) const
{
    DCT_ID id;
    auto varint = WrapVarInt(id.v);
    if (ReadBy<CreationTx, uint256>(txid, varint)) {
        auto tokenImpl = ReadBy<ID, CTokenImpl>(varint);
        if (tokenImpl)
            return { std::make_pair(id, std::move(*tokenImpl))};
    }
    return {};
}

std::unique_ptr<CToken> CTokensView::GetTokenGuessId(const std::string & str, DCT_ID & id) const
{
    std::string const key = trim_ws(str);

    if (key.empty()) {
        id = DCT_ID{0};
        return GetToken(DCT_ID{0});
    }
    if (ParseUInt32(key, &id.v))
        return GetToken(id);

    uint256 tx;
    if (ParseHashStr(key, tx)) {
        auto pair = GetTokenByCreationTx(tx);
        if (pair) {
            id = pair->first;
            return MakeUnique<CTokenImpl>(pair->second);
        }
    }
    else {
        auto pair = GetToken(key);
        if (pair) {
            id = pair->first;
            return std::move(pair->second);
        }
    }
    return {};
}

void CTokensView::ForEachToken(std::function<bool (const DCT_ID &, const CToken &)> callback, DCT_ID const & start)
{
    DCT_ID tokenId = start;
    auto hint = WrapVarInt(tokenId.v);

    ForEach<ID, CVarInt<VarIntMode::DEFAULT, uint32_t>, CTokenImpl>([&tokenId, &callback] (CVarInt<VarIntMode::DEFAULT, uint32_t> const &, CTokenImpl tokenImpl) {
        return callback(tokenId, tokenImpl);
    }, hint);

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
    WriteBy<ID>(WrapVarInt(id.v), token);
    WriteBy<Symbol>(token.symbol, WrapVarInt(id.v));
    WriteBy<CreationTx>(token.creationTx, WrapVarInt(id.v));
    return Res::Ok();
}

ResVal<DCT_ID> CTokensView::CreateToken(const CTokensView::CTokenImpl & token)
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
        ForEachToken([&](DCT_ID const& currentId, CToken const& token) {
            if(currentId < DCT_ID_START)
                id.v = currentId.v + 1;
            return currentId < DCT_ID_START;
        }, id);
        if (id == DCT_ID_START) {
            id = IncrementLastDctId();
            LogPrintf("Warning! Range <DCT_ID_START already filled. Using \"common\" id=%s for new token\n", id.ToString().c_str());
        }
    }
    else
        id = IncrementLastDctId();

    std::string symbolKey = token.CreateSymbolKey(id);

    WriteBy<ID>(WrapVarInt(id.v), token);
    WriteBy<Symbol>(symbolKey, WrapVarInt(id.v));
    WriteBy<CreationTx>(token.creationTx, WrapVarInt(id.v));
    return {id, Res::Ok()};
}

/// @deprecated used only by tests. rewrite tests
bool CTokensView::RevertCreateToken(const uint256 & txid)
{
    auto pair = GetTokenByCreationTx(txid);
    if (!pair) {
        LogPrintf("Token creation revert error: token with creation tx %s does not exist!\n", txid.ToString());
        return false;
    }
    DCT_ID id = pair->first;
    auto lastId = ReadLastDctId();
    if (!lastId || (*lastId) != id) {
        LogPrintf("Token creation revert error: revert sequence broken! (txid = %s, id = %s, LastDctId = %s)\n", txid.ToString(), id.ToString(), (lastId ? lastId->ToString() : DCT_ID{0}.ToString()));
        return false;
    }
    auto const & token = pair->second;
    EraseBy<ID>(WrapVarInt(id.v));
    EraseBy<Symbol>(token.symbol);
    EraseBy<CreationTx>(token.creationTx);
    DecrementLastDctId();
    return true;
}

Res CTokensView::UpdateToken(const uint256 &tokenTx, CToken & newToken)
{
    auto pair = GetTokenByCreationTx(tokenTx);
    if (!pair) {
        return Res::Err("token with creationTx %s does not exist!", tokenTx.ToString());
    }
    DCT_ID id = pair->first;
    CTokenImpl & oldToken = pair->second;

    if (oldToken.IsFinalized()) {
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
        WriteBy<Symbol>(newSymbolKey, WrapVarInt(id.v));
    }

    // apply DAT flag and symbol only AFTER dealing with symbol indexes:
    oldToken.symbol = newToken.symbol;
    oldToken.flags = newToken.IsDAT() ?
                     oldToken.flags | (uint8_t)CToken::TokenFlags::DAT :
                     oldToken.flags & ~(uint8_t)CToken::TokenFlags::DAT;

    // regular flags:
    oldToken.flags = newToken.IsMintable() ?
                         oldToken.flags | (uint8_t)CToken::TokenFlags::Mintable :
                         oldToken.flags & ~(uint8_t)CToken::TokenFlags::Mintable;
    oldToken.flags = newToken.IsTradeable() ?
                         oldToken.flags | (uint8_t)CToken::TokenFlags::Tradeable :
                         oldToken.flags & ~(uint8_t)CToken::TokenFlags::Tradeable;
    oldToken.flags = newToken.IsFinalized() ?
                         oldToken.flags | (uint8_t)CToken::TokenFlags::Finalized :
                         oldToken.flags;

    WriteBy<ID>(WrapVarInt(id.v), oldToken);
    return Res::Ok();
}

DCT_ID CTokensView::IncrementLastDctId()
{
    DCT_ID result{DCT_ID_START};
    auto lastDctId = ReadLastDctId();
    if (lastDctId) {
        result = DCT_ID{std::max(lastDctId->v + 1, result.v)};
    }
    assert (Write(LastDctId::prefix, result));
    return result;
}

/// @deprecated used only by "revert*". rewrite tests
DCT_ID CTokensView::DecrementLastDctId()
{
    auto lastDctId = ReadLastDctId();
    if (lastDctId && *lastDctId >= DCT_ID_START) {
        --(lastDctId->v);
    }
    else {
        LogPrintf("Critical fault: trying to decrement nonexistent DCT_ID or it is lower than DCT_ID_START\n");
        assert (false);
    }
    assert (Write(LastDctId::prefix, *lastDctId)); // it is ok if (DCT_ID_START - 1) will be written
    return *lastDctId;
}

boost::optional<DCT_ID> CTokensView::ReadLastDctId() const
{
    DCT_ID lastDctId{DCT_ID_START};
    if (Read(LastDctId::prefix, lastDctId)) {
        return {lastDctId};
    }
    return {};
}


