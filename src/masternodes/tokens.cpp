// Copyright (c) 2019 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/tokens.h>
#include <core_io.h>
#include <primitives/transaction.h>

/// @attention make sure that it does not overlap with those in masternodes.cpp/orders.cpp !!!
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

void CStableTokens::Initialize(CStableTokens & dst)
{
    // Default CToken()
    //    : symbol("")
    //    , name("")
    //    , decimal(8)
    //    , limit(0)
    //    , flags(uint8_t(TokenFlags::Default))

    CToken DFI;
    DFI.symbol = CURRENCY_UNIT;
    DFI.name = "Default Defi token";
    /// ? what about Mintable|Tradable???

    dst.tokens[DCT_ID{0}] = DFI;
    dst.indexedBySymbol[DFI.symbol] = DCT_ID{0};
}

std::unique_ptr<CToken> CStableTokens::GetToken(DCT_ID id) const
{
    auto it = tokens.find(id);
    if (it != tokens.end()) {
        return MakeUnique<CToken>(it->second);
    }
    return {};
}

boost::optional<std::pair<DCT_ID, std::unique_ptr<CToken>>> CStableTokens::GetToken(const std::string & symbol) const
{
    auto it = indexedBySymbol.find(symbol);
    if (it != indexedBySymbol.end()) {
        auto token = GetToken(it->second);
        assert(token);
        return { std::make_pair(it->second, std::move(token)) };
    }
    return {};

}

bool CStableTokens::ForEach(std::function<bool (const DCT_ID &, CToken const &)> callback, DCT_ID const & start) const
{
    for (auto && it = tokens.lower_bound(start); it != tokens.end(); ++it) {
        if (!callback(it->first, it->second))
            return false;
    }
    return true;
}

const CStableTokens & CStableTokens::Instance()
{
    static CStableTokens dst;
    static std::once_flag initialized;
    std::call_once (initialized, CStableTokens::Initialize, dst);
    return dst;
}

std::unique_ptr<CToken> CTokensView::GetToken(DCT_ID id) const
{
    if (id < DCT_ID_START) {
        return CStableTokens::Instance().GetToken(id);
    }
    auto tokenImpl = ReadBy<ID, CTokenImpl>(WrapVarInt(id.v)); // @todo change serialization of DCT_ID to VarInt by default?
    if (tokenImpl)
        return MakeUnique<CTokenImpl>(*tokenImpl);

    return {};
}

boost::optional<std::pair<DCT_ID, std::unique_ptr<CToken> > > CTokensView::GetToken(const std::string & symbol) const
{
    auto dst = CStableTokens::Instance().GetToken(symbol);
    if (dst)
        return dst;

    DCT_ID id;
    auto varint = WrapVarInt(id.v);
    if (ReadBy<Symbol, std::string>(symbol, varint)) {
        assert(id >= DCT_ID_START);
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
    if (!CStableTokens::Instance().ForEach([&] (const DCT_ID & id, CToken const & token) {
        return callback(id, token);
    }, start)) {
        return; // if was inturrupted
    }

    DCT_ID tokenId = start;
    auto hint = WrapVarInt(tokenId.v);

    ForEach<ID, CVarInt<VarIntMode::DEFAULT, uint32_t>, CTokenImpl>([&tokenId, &callback] (CVarInt<VarIntMode::DEFAULT, uint32_t> const &, CTokenImpl tokenImpl) {
        return callback(tokenId, tokenImpl);
    }, hint);

}

Res CTokensView::CreateToken(const CTokensView::CTokenImpl & token)
{
    if (GetToken(token.symbol)) {
        return Res::Err("token '%s' already exists!", token.symbol);
    }
    // this should not happen, but for sure
    if (GetTokenByCreationTx(token.creationTx)) {
        return Res::Err("token with creation tx %s already exists!", token.creationTx.ToString());
    }
    DCT_ID id = IncrementLastDctId();
    WriteBy<ID>(WrapVarInt(id.v), token);
    WriteBy<Symbol>(token.symbol, WrapVarInt(id.v));
    WriteBy<CreationTx>(token.creationTx, WrapVarInt(id.v));
    return Res::Ok();
}

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

Res CTokensView::DestroyToken(uint256 const & tokenTx, const uint256 & txid, int height)
{
    auto pair = GetTokenByCreationTx(tokenTx);
    if (!pair) {
        return Res::Err("token with creationTx %s does not exist!", tokenTx.ToString());
    }
    /// @todo token: check for token supply / utxos

    CTokenImpl & tokenImpl = pair->second;
    if (tokenImpl.destructionTx != uint256{}) {
        return Res::Err("token with creationTx %s was already destroyed by tx %s!", tokenTx.ToString(), tokenImpl.destructionTx.ToString());
    }

    tokenImpl.destructionTx = txid;
    tokenImpl.destructionHeight = height;
    WriteBy<ID>(WrapVarInt(pair->first.v), tokenImpl);
    return Res::Ok();
}

bool CTokensView::RevertDestroyToken(uint256 const & tokenTx, const uint256 & txid)
{
    auto pair = GetTokenByCreationTx(tokenTx);
    if (!pair) {
        LogPrintf("Token destruction revert error: token with creationTx %s does not exist!\n", tokenTx.ToString());
        return false;
    }
    CTokenImpl & tokenImpl = pair->second;
    if (tokenImpl.destructionTx != txid) {
        LogPrintf("Token destruction revert error: token with creationTx %s was not destroyed by tx %s!\n", tokenTx.ToString(), txid.ToString());
        return false;
    }

    tokenImpl.destructionTx = uint256{};
    tokenImpl.destructionHeight = -1;
    WriteBy<ID>(WrapVarInt(pair->first.v), tokenImpl);
    return true;
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


