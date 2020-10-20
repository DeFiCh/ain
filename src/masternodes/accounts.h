// Copyright (c) 2020 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ACCOUNTS_H
#define DEFI_MASTERNODES_ACCOUNTS_H

#include <flushablestorage.h>
#include <masternodes/res.h>
#include <masternodes/balances.h>
#include <amount.h>
#include <script/script.h>

class CAccountsView : public virtual CStorageView
{
public:
    void ForEachBalance(std::function<bool(CScript const & owner, CTokenAmount const & amount)> callback, BalanceKey start) const;
    CTokenAmount GetBalance(CScript const & owner, DCT_ID tokenID) const;

    Res SetBalance(CScript const & owner, CTokenAmount amount);
    Res AddBalance(CScript const & owner, CTokenAmount amount);
    Res AddBalances(CScript const & owner, CBalances const & balances);
    Res SubBalance(CScript const & owner, CTokenAmount amount);
    Res SubBalances(CScript const & owner, CBalances const & balances);

    // tags
    struct ByBalanceKey { static const unsigned char prefix; };
};

#endif //DEFI_MASTERNODES_ACCOUNTS_H
