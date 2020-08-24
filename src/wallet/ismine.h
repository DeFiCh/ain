// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_WALLET_ISMINE_H
#define DEFI_WALLET_ISMINE_H

#include <script/standard.h>

#include <stdint.h>
#include <bitset>

class CWallet;
class CScript;

/** IsMine() return codes */
enum isminetype : unsigned int
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

/**
 * Cachable amount subdivided into watchonly and spendable parts.
 */
struct CachableAmount
{
    // NO and ALL are never (supposed to be) cached
    std::bitset<ISMINE_ENUM_ELEMENTS> m_cached;
    TAmounts m_value[ISMINE_ENUM_ELEMENTS];
    inline void Reset()
    {
        m_cached.reset();
    }
//    void Set(isminefilter filter, CAmount value)
//    {
//        m_cached.set(filter);
//        m_value[filter] = value;
//    }
    void Set(isminefilter filter, TAmounts && amounts)
    {
        m_cached.set(filter);
        m_value[filter] = amounts;
    }
};

#endif // DEFI_WALLET_ISMINE_H
