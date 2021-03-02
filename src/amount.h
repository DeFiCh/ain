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
        READWRITE(v);
    }
};

static const CAmount COIN = 100000000;

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
    if (res / w != a || res > std::numeric_limits<int64_t>::max()) {
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
            return Res::Err("negative amount: %d", amount);
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
            return Res::Err("negative amount: %d", amount);
        }
        if (this->nValue < amount) {
            return Res::Err("amount %d is less than %d", this->nValue, amount);
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
