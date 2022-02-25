// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_WALLET_ISMINE_H
#define DEFI_WALLET_ISMINE_H

#include <script/standard.h>

#include <stdint.h>
#include <bitset>
#include <unordered_map>

class CWallet;
class CScript;

/** IsMine() return codes */
enum isminetype : uint8_t
{
    ISMINE_NO         = 0,
    ISMINE_WATCH_ONLY = 1 << 0,
    ISMINE_SPENDABLE  = 1 << 1,
    ISMINE_USED       = 1 << 2,
    ISMINE_ALL        = ISMINE_WATCH_ONLY | ISMINE_SPENDABLE,
    ISMINE_ALL_USED   = ISMINE_ALL | ISMINE_USED,
    ISMINE_ENUM_ELEMENTS,
};
/** used for bitflags of isminetype */
typedef uint8_t isminefilter;

isminetype IsMine(const CWallet& wallet, const CScript& scriptPubKey);
isminetype IsMine(const CWallet& wallet, const CTxDestination& dest);
isminetype IsMineCached(const CWallet& wallet, const CScript& script);
isminetype IsMineCached(const CWallet& wallet, const CTxDestination& dest);

/**
 * Cachable amount subdivided into watchonly and spendable parts.
 */
struct CachableAmount
{
    std::unordered_map<uint8_t, TAmounts> m_value;

    inline void Reset()
    {
        m_value.clear();
    }
    bool IsSet(isminefilter filter) const
    {
        return m_value.count(filter) > 0;
    }
    const TAmounts& Get(isminefilter filter) const
    {
        return m_value.at(filter);
    }
    void Set(isminefilter filter, TAmounts && amounts)
    {
        m_value[filter] = std::move(amounts);
    }
};

struct CScriptHash
{
    uint32_t operator()(const CScript& script) const;
};

#endif // DEFI_WALLET_ISMINE_H
