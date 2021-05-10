// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

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
    void ForEachAccount(std::function<bool(CScript const &)> callback, CScript const & start = {});
    void ForEachBalance(std::function<bool(CScript const &, CTokenAmount const &)> callback, BalanceKey const & start = {});
    CTokenAmount GetBalance(CScript const & owner, DCT_ID tokenID) const;

    virtual Res AddBalance(CScript const & owner, CTokenAmount amount);
    virtual Res SubBalance(CScript const & owner, CTokenAmount amount);

    Res AddBalances(CScript const & owner, CBalances const & balances);
    Res SubBalances(CScript const & owner, CBalances const & balances);

    uint32_t GetBalancesHeight(CScript const & owner);
    Res UpdateBalancesHeight(CScript const & owner, uint32_t height);

    // tags
    struct ByBalanceKey { static const unsigned char prefix; };
    struct ByHeightKey { static const unsigned char prefix; };

private:
    Res SetBalance(CScript const & owner, CTokenAmount amount);
};

#endif //DEFI_MASTERNODES_ACCOUNTS_H
