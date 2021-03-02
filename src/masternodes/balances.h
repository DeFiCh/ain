// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_BALANCES_H
#define DEFI_MASTERNODES_BALANCES_H

#include <cstdint>
#include <amount.h>
#include <serialize.h>
#include <script/script.h>

struct CBalances
{
    TAmounts balances;

    Res Add(CTokenAmount amount) {
        if (amount.nValue == 0) {
            return Res::Ok();
        }
        auto current = CTokenAmount{amount.nTokenId, balances[amount.nTokenId]};
        auto res = current.Add(amount.nValue);
        if (!res.ok) {
            return res;
        }
        if (current.nValue == 0) {
            balances.erase(amount.nTokenId);
        } else {
            balances[amount.nTokenId] = current.nValue;
        }
        return Res::Ok();
    }
    Res Sub(CTokenAmount amount) {
        if (amount.nValue == 0) {
            return Res::Ok();
        }
        auto current = CTokenAmount{amount.nTokenId, balances[amount.nTokenId]};
        auto res = current.Sub(amount.nValue);
        if (!res.ok) {
            return res;
        }
        if (current.nValue == 0) {
            balances.erase(amount.nTokenId);
        } else {
            balances[amount.nTokenId] = current.nValue;
        }
        return Res::Ok();
    }
    CTokenAmount SubWithRemainder(CTokenAmount amount) {
        if (amount.nValue == 0) {
            return CTokenAmount{amount.nTokenId, 0};
        }
        auto current = CTokenAmount{amount.nTokenId, balances[amount.nTokenId]};
        auto remainder = current.SubWithRemainder(amount.nValue);
        if (current.nValue == 0) {
            balances.erase(amount.nTokenId);
        } else {
            balances[amount.nTokenId] = current.nValue;
        }
        return CTokenAmount{amount.nTokenId, remainder};
    }
    Res SubBalances(TAmounts const & other) {
        for (const auto& kv : other) {
            auto res = Sub(CTokenAmount{kv.first, kv.second});
            if (!res.ok) {
                return res;
            }
        }
        return Res::Ok();
    }
    CBalances SubBalancesWithRemainder(TAmounts const & other) {
        CBalances remainderBalances;
        for (const auto& kv : other) {
            CTokenAmount remainder = SubWithRemainder(CTokenAmount{kv.first, kv.second});
            // if remainder token value is zero
            // this addition won't get any effect
            remainderBalances.Add(remainder);
        }
        return remainderBalances;
    }
    Res AddBalances(TAmounts const & other) {
        for (const auto& kv : other) {
            auto res = Add(CTokenAmount{kv.first, kv.second});
            if (!res.ok) {
                return res;
            }
        }
        return Res::Ok();
    }

    std::string ToString() const {
        std::string str;
        str.reserve(100);
        for (const auto& kv : balances) {
            if (!str.empty()) {
                str += ",";
            }
            str += CTokenAmount{kv.first, kv.second}.ToString();
        }
        return str;
    }

    static CBalances Sum(std::vector<CTokenAmount> const & tokens) {
        CBalances res;
        for (const auto& token : tokens) {
            res.Add(token);
        }
        return res;
    }

    friend bool operator==(const CBalances& a, const CBalances& b) {
        return a.balances == b.balances;
    }

    friend bool operator!=(const CBalances& a, const CBalances& b) {
        return a.balances != b.balances;
    }

    // NOTE: if some balance from b is hgher than a => a is less than b
    friend bool operator<(const CBalances& a, const CBalances& b) {
        for (const auto& b_kv : b.balances) {
            const auto a_value_it = a.balances.find(b_kv.first);
            CAmount a_value = 0;
            if (a_value_it != a.balances.end()) {
                a_value = a_value_it->second;
            }
            if (b_kv.second > a_value) {
                return true;
            }
        }
        return false;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(balances);
        if (ser_action.ForRead()) {
            // check that no zero values are written
            const size_t sizeOriginal = balances.size();
            TrimZeros();
            const size_t sizeStripped = balances.size();
            if (sizeOriginal != sizeStripped) {
                throw std::ios_base::failure("non-canonical balances (zero amount)");
            }
        }
    }

private:
    // TrimZeros is private because balances cannot have zeros normally
    void TrimZeros() {
        for (auto it = balances.begin(); it != balances.end(); /* no advance */) {
            if (it->second == 0) {
                it = balances.erase(it);
            }
            else
                it++;
        }
    }
};

struct CAccountToUtxosMessage {
    CScript from;
    CBalances balances;
    uint32_t mintingOutputsStart;

    ADD_SERIALIZE_METHODS;

    std::string ToString() const {
        if (balances.balances.empty()) {
            return "empty transfer";
        }
        return "from " + from.GetHex() + " to UTXOs " + balances.ToString();
    }

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(from);
        READWRITE(balances);
        READWRITE(VARINT(mintingOutputsStart));
    }
};

using CAccounts = std::map<CScript, CBalances>;

struct CAccountToAccountMessage {
    CScript from;
    CAccounts to; // to -> balances

    std::string ToString() const {
        if (to.empty()) {
            return "empty transfer";
        }
        std::string result = "from " + from.GetHex() + " to ";
        for (const auto& kv : to) {
            result += "(" + kv.first.GetHex() + "->" + kv.second.ToString() + ")";
        }
        return result;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(from);
        READWRITE(to);
    }
};

struct CAnyAccountsToAccountsMessage {
    CAccounts from; // from -> balances
    CAccounts to; // to -> balances

    std::string ToString() const {
        if (from.empty() || to.empty()) {
            return "empty transfer";
        }
        std::string result = "from ";
        for (const auto& kv : from) {
            result += "(" + kv.first.GetHex() + "->" + kv.second.ToString() + ")";
        }
        result += " to ";
        for (const auto& kv : to) {
            result += "(" + kv.first.GetHex() + "->" + kv.second.ToString() + ")";
        }
        return result;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(from);
        READWRITE(to);
    }
};

struct CUtxosToAccountMessage {
    CAccounts to; // to -> balances

    std::string ToString() const {
        if (to.empty()) {
            return "empty transfer";
        }
        std::string result = "from UTXOs to ";
        for (const auto& kv : to) {
            result += "(" + kv.first.GetHex() + "->" + kv.second.ToString() + ")";
        }
        return result;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(to);
    }
};

inline CBalances SumAllTransfers(CAccounts const & to) {
    CBalances sum;
    for (const auto& kv : to) {
        sum.AddBalances(kv.second.balances);
    }
    return sum;
}

struct BalanceKey {
    CScript owner;
    DCT_ID tokenID;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(owner);
        READWRITE(WrapBigEndian(tokenID.v));
    }
};
#endif //DEFI_MASTERNODES_BALANCES_H
