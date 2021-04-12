// Copyright (c) 2020 The DeFi Foundation
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_TOKENS_H
#define DEFI_MASTERNODES_TOKENS_H

#include <flushablestorage.h>

#include <amount.h>
#include <masternodes/res.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

class CTransaction;
class UniValue;

std::string trim_ws(std::string const & str);

class CToken
{
public:
    static const uint8_t MAX_TOKEN_NAME_LENGTH = 128;
    static const uint8_t MAX_TOKEN_SYMBOL_LENGTH = 8;
    enum class TokenFlags : uint8_t
    {
        None = 0,
        Mintable = 0x01,
        Tradeable = 0x02,
        DAT = 0x04,
        LPS = 0x08, // Liquidity Pool Share
        Finalized = 0x10, // locked forever
        Default = TokenFlags::Mintable | TokenFlags::Tradeable
    };

    //! basic properties
    std::string symbol;
    std::string name;
    uint8_t decimal;    // now fixed to 8
    CAmount limit;      // now isn't tracked
    uint8_t flags;      // minting support, tradeability

    CToken()
        : symbol("")
        , name("")
        , decimal(8)
        , limit(0)
        , flags(uint8_t(TokenFlags::Default))
    {}
    virtual ~CToken() = default;

    inline bool IsMintable() const
    {
        return flags & (uint8_t)TokenFlags::Mintable;
    }
    inline bool IsTradeable() const
    {
        return flags & (uint8_t)TokenFlags::Tradeable;
    }
    inline bool IsDAT() const
    {
        return flags & (uint8_t)TokenFlags::DAT;
    }
    inline bool IsPoolShare() const
    {
        return flags & (uint8_t)TokenFlags::LPS;
    }
    inline bool IsFinalized() const
    {
        return flags & (uint8_t)TokenFlags::Finalized;
    }
    inline Res IsValidSymbol() const
    {
        if (symbol.size() == 0 || IsDigit(symbol[0])) {
            return Res::Err("token symbol should be non-empty and starts with a letter");
        }
        if (symbol.find('#') != std::string::npos) {
            return Res::Err("token symbol should not contain '#'");
        }
        return Res::Ok();
    }
    inline std::string CreateSymbolKey(DCT_ID const & id) const {
        return symbol + (IsDAT() ? "" : "#" + std::to_string(id.v));
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(symbol);
        READWRITE(name);
        READWRITE(decimal);
        READWRITE(limit);
        READWRITE(flags);
    }
};

class CTokenImplementation : public CToken
{
public:
    //! tx related properties
    CAmount minted;
    uint256 creationTx;
    uint256 destructionTx;
    int32_t creationHeight; // @todo use unsigned integers, because serialization of signed integers isn't fulled defined
    int32_t destructionHeight;

    CTokenImplementation()
        : CToken()
        , minted(0)
        , creationTx()
        , destructionTx()
        , creationHeight(-1)
        , destructionHeight(-1)
    {}
    ~CTokenImplementation() override = default;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEAS(CToken, *this);
        READWRITE(minted);
        READWRITE(creationTx);
        READWRITE(destructionTx);
        READWRITE(creationHeight);
        READWRITE(destructionHeight);
    }
};

class CTokensView : public virtual CStorageView
{
public:
    static const DCT_ID DCT_ID_START; // = 128;
    static const unsigned char DB_TOKEN_LASTID; // = 'L';

    using CTokenImpl = CTokenImplementation;
    std::unique_ptr<CToken> GetToken(DCT_ID id) const;
    boost::optional<std::pair<DCT_ID, std::unique_ptr<CToken>>> GetToken(std::string const & symbol) const;
    // the only possible type of token (with creationTx) is CTokenImpl
    boost::optional<std::pair<DCT_ID, CTokenImpl>> GetTokenByCreationTx(uint256 const & txid) const;
    std::unique_ptr<CToken> GetTokenGuessId(const std::string & str, DCT_ID & id) const;

    void ForEachToken(std::function<bool(DCT_ID const &, CLazySerialize<CTokenImpl>)> callback, DCT_ID const & start = DCT_ID{0});

    Res CreateDFIToken();
    ResVal<DCT_ID> CreateToken(CTokenImpl const & token, bool isPreBayfront);
    Res RevertCreateToken(uint256 const & txid);
    Res UpdateToken(uint256 const & tokenTx, CToken const & newToken, bool isPreBayfront);

    Res BayfrontFlagsCleanup();
    Res AddMintedTokens(uint256 const & tokenTx, CAmount const & amount);

    // tags
    struct ID { static const unsigned char prefix; };
    struct Symbol { static const unsigned char prefix; };
    struct CreationTx { static const unsigned char prefix; };
    struct LastDctId { static const unsigned char prefix; };

private:
    // have to incapsulate "last token id" related methods here
    DCT_ID IncrementLastDctId();
    DCT_ID DecrementLastDctId();
    boost::optional<DCT_ID> ReadLastDctId() const;
};

#endif // DEFI_MASTERNODES_TOKENS_H
