// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_AMOUNT_H
#define DEFI_AMOUNT_H

#include <stdint.h>
#include <util/strencodings.h>
#include <serialize.h>
#include <masternodes/res.h>

#include <map>

/** Amount in satoshis (Can be negative) */
typedef int64_t CAmount;

struct CURRENCY_ID {
    enum class CurrencyId: uint32_t {
        UNKNOWN = 0,
        USD,
        EUR,
    };

    uint32_t v;
    static constexpr auto CURRENCY_USD = "USD";
    static constexpr auto CURRENCY_EUR = "EUR";
    static constexpr auto CURRENCY_UNKNOWN = "UNKNOWN";

    static CURRENCY_ID USD() {
        return CURRENCY_ID{CurrencyId::USD};
    };

    static CURRENCY_ID EUR() {
        return CURRENCY_ID{CurrencyId::EUR};
    };

    static CURRENCY_ID INVALID() {
        return CURRENCY_ID{CurrencyId::UNKNOWN};
    }

    explicit CURRENCY_ID(CurrencyId id) : v{static_cast<uint32_t>(id)} {}

    CURRENCY_ID() : v{static_cast<uint32_t>(CurrencyId::UNKNOWN)} {}

    std::string ToString() const {
        switch (v) {
            case static_cast<uint32_t>(CurrencyId::USD):
                return CURRENCY_USD;
            case static_cast<uint32_t>(CurrencyId::EUR):
                return CURRENCY_EUR;
            case static_cast<uint32_t>(CurrencyId::UNKNOWN):
                break;
        }
        return CURRENCY_UNKNOWN;
    }

    static CURRENCY_ID FromString(const std::string &name) {
        if (name == CURRENCY_USD) {
            return CURRENCY_ID(CurrencyId::USD);
        }
        if (name == CURRENCY_EUR) {
            return CURRENCY_ID(CurrencyId::EUR);
        }

        return CURRENCY_ID(CurrencyId::UNKNOWN);
    }

    bool IsValid() const {
        return v != static_cast<uint32_t>(CurrencyId::UNKNOWN);
    }

    bool operator<(const CURRENCY_ID& other) const {
        return v < other.v;
    }

    inline bool operator==(const CURRENCY_ID& other) const {
        return v == other.v;
    }

    inline bool operator!=(const CURRENCY_ID& other) const {
        return v != other.v;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(v);
    }
};

// Defi Custom Token ID
struct DCT_ID {
    uint32_t v;

    std::string ToString() const {
        //uint32_t v_be = htobe32(v);
        //return HexStr(&v_be, &v_be + sizeof(v_be));
        return std::to_string(v);
    }

    static ResVal<DCT_ID> FromString(std::string const & str) noexcept {
        try {
            return {DCT_ID{(uint32_t) std::stoul(str)}, Res::Ok()};
        } catch (std::exception& e) {
            return Res::Err("failed to deserialize DCT_ID: %s", e.what());
        } catch (...) {
            return Res::Err("failed to deserialize DCT_ID");
        }
    }

    friend bool operator<(const DCT_ID& a, const DCT_ID& b)
    {
        return a.v < b.v;
    }

    friend bool operator>(const DCT_ID& a, const DCT_ID& b)
    {
        return a.v > b.v;
    }

    friend bool operator<=(const DCT_ID& a, const DCT_ID& b)
    {
        return a.v <= b.v;
    }

    friend bool operator>=(const DCT_ID& a, const DCT_ID& b)
    {
        return a.v >= b.v;
    }

    friend bool operator==(const DCT_ID& a, const DCT_ID& b)
    {
        return a.v == b.v;
    }

    friend bool operator!=(const DCT_ID& a, const DCT_ID& b)
    {
        return !(a == b);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(v);
    }
};

static const CAmount COIN = 100000000;

//Converts the given value to decimal format string with COIN precision. 
inline std::string GetDecimaleString(CAmount nValue)
{
    const bool sign = nValue < 0;
    const int64_t n_abs = (sign ? -nValue : nValue);
    const int64_t quotient = n_abs / COIN;
    const int64_t remainder = n_abs % COIN;
    return strprintf("%s%d.%08d", sign ? "-" : "", quotient, remainder);
}

typedef std::map<DCT_ID, CAmount> TAmounts;

inline ResVal<CAmount> SafeAdd(CAmount _a, CAmount _b) {
    // check limits
    if (_a < 0 || _b < 0) {
        return Res::Err("negative amount");
    }
    // convert to unsigned, because signed overflow is UB
    const uint64_t a = (uint64_t) _a;
    const uint64_t b = (uint64_t) _b;

    const uint64_t sum = a + b;
    // check overflow
    if ((sum - a) != b || ((uint64_t)std::numeric_limits<CAmount>::max()) < sum) {
        return Res::Err("overflow");
    }
    return {(CAmount) sum, Res::Ok()};
}

inline ResVal<CAmount> SafeMultiply(CAmount _a, uint64_t w) {
    if (_a < 0) {
        return Res::Err("negative amount");
    }

    auto a = static_cast<uint64_t>(_a);
    uint64_t res = a * w;
    constexpr uint64_t int64Max = std::numeric_limits<int64_t>::max();
    if (res / w != a || res > static_cast<uint64_t>(int64Max)) {
        return Res::Err("overflow");
    }

    return {static_cast<CAmount>(res), Res::Ok()};
}

struct CTokenAmount { // simple std::pair is less informative
    DCT_ID nTokenId;
    CAmount nValue;

    std::string ToString() const {
        const bool sign = nValue < 0;
        const int64_t n_abs = (sign ? -nValue : nValue);
        const int64_t quotient = n_abs / COIN;
        const int64_t remainder = n_abs % COIN;
        return strprintf("%s%d.%08d@%d", sign ? "-" : "", quotient, remainder, nTokenId.v);
    }

    Res Add(CAmount amount) {
        // safety checks
        if (amount < 0) {
            return Res::Err("negative amount: %s", GetDecimaleString(amount));
        }
        // add
        auto sumRes = SafeAdd(this->nValue, amount);
        if (!sumRes.ok) {
            return sumRes.res();
        }
        this->nValue = *sumRes.val;
        return Res::Ok();
    }
    Res Sub(CAmount amount) {
        // safety checks
        if (amount < 0) {
            return Res::Err("negative amount: %s", GetDecimaleString(amount));
        }
        if (this->nValue < amount) {
            return Res::Err("amount %s is less than %s", GetDecimaleString(this->nValue), GetDecimaleString(amount));
        }
        // sub
        this->nValue -= amount;
        return Res::Ok();
    }
    CAmount SubWithRemainder(CAmount amount) {
        // safety checks
        if (amount < 0) {
            Add(-amount);
            return 0;
        }
        if (this->nValue < amount) {
            CAmount remainder = amount - this->nValue;
            this->nValue = 0;
            return remainder;
        }
        // sub
        this->nValue -= amount;
        return 0;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(VARINT(nTokenId.v));
        READWRITE(nValue);
    }

    friend bool operator==(const CTokenAmount& a, const CTokenAmount& b)
    {
        return a.nTokenId == b.nTokenId && a.nValue == b.nValue;
    }

    friend bool operator!=(const CTokenAmount& a, const CTokenAmount& b)
    {
        return !(a == b);
    }
};

inline std::ostream& operator << (std::ostream &os, const CTokenAmount &ta)
{
    return os << ta.ToString();
}

/** No amount larger than this (in satoshi) is valid.
 *
 * Note that this constant is *not* the total money supply, which in Defi
 * currently happens to be less than 21,000,000 DFI for various reasons, but
 * rather a sanity check. As this sanity check is used by consensus-critical
 * validation code, the exact value of the MAX_MONEY constant is consensus
 * critical; in unusual circumstances like a(nother) overflow bug that allowed
 * for the creation of coins out of thin air modification could lead to a fork.
 * */
static const CAmount MAX_MONEY = 1200000000 * COIN; // (1.2B) - old 21000000 * 4
inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

#endif //  DEFI_AMOUNT_H
