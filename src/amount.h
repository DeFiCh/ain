// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_AMOUNT_H
#define DEFI_AMOUNT_H

#include <arith_uint256.h>
#include <masternodes/res.h>
#include <serialize.h>
#include <stdint.h>
#include <util/strencodings.h>

#include <map>

/** Amount in satoshis (Can be negative) */
typedef int64_t CAmount;

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
        READWRITE(VARINT(v));
    }
};

constexpr CAmount COIN = 100000000;

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

template<typename T>
ResVal<T> SafeAdd(T a, T b) {
    static_assert(std::is_integral_v<T>, "SafeAdd is implemented to integral types");

    // check limits
    if (a < 0 || b < 0) {
        return Res::Err("negative amount");
    }

    constexpr auto max = std::numeric_limits<T>::max();
    // check overflow
    if (max - a < b) {
        return Res::Err("integral overflow");
    }
    return {a + b, Res::Ok()};
}

inline CAmount MultiplyAmounts(CAmount a, CAmount b)
{
    return (arith_uint256(a) * arith_uint256(b) / arith_uint256(COIN)).GetLow64();
}

inline CAmount DivideAmounts(CAmount a, CAmount b)
{
    return (arith_uint256(a) * arith_uint256(COIN) / arith_uint256(b)).GetLow64();
}

struct CTokenAmount { // simple std::pair is less informative
    DCT_ID nTokenId;
    CAmount nValue;

    std::string ToString() const {
        return strprintf("%s@%d", GetDecimaleString(nValue), nTokenId.v);
    }

    Res Add(CAmount amount) {
        // safety checks
        if (amount < 0) {
            return Res::Err("negative amount: %s", GetDecimaleString(amount));
        }
        // add
        auto sumRes = SafeAdd(nValue, amount);
        if (!sumRes) {
            return std::move(sumRes);
        }
        nValue = sumRes;
        return Res::Ok();
    }
    Res Sub(CAmount amount) {
        // safety checks
        if (amount < 0) {
            return Res::Err("negative amount: %s", GetDecimaleString(amount));
        }
        if (nValue < amount) {
            return Res::Err("amount %s is less than %s", GetDecimaleString(nValue), GetDecimaleString(amount));
        }
        // sub
        nValue -= amount;
        return Res::Ok();
    }
    CAmount SubWithRemainder(CAmount amount) {
        // safety checks
        if (amount < 0) {
            Add(-amount);
            return 0;
        }
        if (nValue < amount) {
            CAmount remainder = amount - nValue;
            nValue = 0;
            return remainder;
        }
        // sub
        nValue -= amount;
        return 0;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nTokenId);
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
inline bool MoneyRange(CAmount nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

#endif //  DEFI_AMOUNT_H
