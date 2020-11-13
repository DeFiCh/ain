// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_AMOUNT_H
#define DEFI_AMOUNT_H

#include <stdint.h>
#include <util/strencodings.h>
#include <serialize.h>
#include <masternodes/res.h>

#include <cctype>
#include <cmath>
#include <map>

/** Amount in satoshis (Can be negative) */
typedef int64_t CAmount;

// Defi Custom Token ID
struct DCT_ID {
    uint32_t v;

    std::string ToString() const {
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

// used by DFI only
static const CAmount COIN = 100000000;

typedef std::map<DCT_ID, CAmount> TAmounts;

inline ResVal<CAmount> SafeAdd(CAmount a, CAmount b) {
    // check limits
    if (a < 0 || b < 0) {
        return Res::Err("negative amount");
    }
    // check overflow
    auto diff = std::numeric_limits<CAmount>::max() - a;
    if (b > diff) {
        return Res::Err("overflow");
    }
    return {a + b, Res::Ok()};
}

struct CTokenAmount { // simple std::pair is less informative
    DCT_ID nTokenId;
    CAmount nValue;

    std::string ToString(uint8_t decimal) const {
        bool sign = nValue < 0;
        auto n_abs = (sign ? -nValue : nValue);
        uint32_t coin = std::pow(10, decimal);
        auto str = strprintf("%d.%.*d", (n_abs / coin), decimal, (n_abs % coin));
        if (nTokenId != DCT_ID{0})
            str += '@' + nTokenId.ToString();
        if (sign)
            str.insert(0, 1, '-');
        return str;
    }

    static ResVal<CTokenAmount> FromString(const std::string& str, uint8_t decimal = 8) {
        CTokenAmount amount{DCT_ID{0}, 0};
        auto token = str.find('@');
        if (token != str.npos) {
            auto start = token;
            const auto ssize = str.size();
            while (++start < ssize && !std::isdigit(str[start]));
            if (start == ssize)
                return Res::Err("Invalid token");
            auto res = DCT_ID::FromString(str.substr(start));
            if (!res) return res;
            amount.nTokenId = res;
        }
        if (!ParseFixedPoint(str.substr(0, token), decimal, &amount.nValue))
            return Res::Err("Invalid amount");
        return {amount, Res::Ok()};
    }

    Res Add(CAmount amount) {
        // safety checks
        if (amount < 0) {
            return Res::Err("negative amount: %d", amount);
        }
        // add
        auto sumRes = SafeAdd(nValue, amount);
        if (!sumRes.ok) {
            return sumRes.res();
        }
        nValue = sumRes;
        return Res::Ok();
    }
    Res Sub(CAmount amount) {
        // safety checks
        if (amount < 0) {
            return Res::Err("negative amount: %d", amount);
        }
        if (nValue < amount) {
            return Res::Err("Amount %d is less than %d", nValue, amount);
        }
        // sub
        nValue -= amount;
        return Res::Ok();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(VARINT(nTokenId.v));
        READWRITE(nValue);
    }
};

/** No amount larger than this (in satoshi) is valid.
 *
 * Note that this constant is *not* the total money supply, which in Defi
 * currently happens to be less than 21,000,000 DFI for various reasons, but
 * rather a sanity check. As this sanity check is used by consensus-critical
 * validation code, the exact value of the MAX_MONEY constant is consensus
 * critical; in unusual circumstances like a(nother) overflow bug that allowed
 * for the creation of coins out of thin air modification could lead to a fork.
 * */
static const uint32_t AMOUNT_BITS = sizeof(CAmount) * 8 - int(std::is_signed<CAmount>::value);
static const uint8_t DECIMAL_LIMIT = 18; // we should not allow lower coin value
static const CAmount MIN_COINS_VALUE = 1000; // every token should have at least 1000 coins
static const CAmount MAX_MONEY = 1200000000 * COIN; // (1.2B) - old 21000000 * 4
inline bool MoneyRange(CAmount nValue, CAmount maxMoney = MAX_MONEY) { return (nValue >= 0 && nValue <= maxMoney); }

#endif //  DEFI_AMOUNT_H
