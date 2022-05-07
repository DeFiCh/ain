// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ACCOUNTS_H
#define DEFI_MASTERNODES_ACCOUNTS_H

#include <amount.h>
#include <flushablestorage.h>
#include <masternodes/balances.h>
#include <masternodes/res.h>
#include <script/script.h>

struct CAccountToAccountMessage {
    CScript from;
    CAccounts to; // to -> balances

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

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(from);
        READWRITE(to);
    }
};

struct CUtxosToAccountMessage {
    CAccounts to; // to -> balances

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(to);
    }
};

struct CAccountToUtxosMessage {
    CScript from;
    CBalances balances;
    uint32_t mintingOutputsStart;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(from);
        READWRITE(balances);
        READWRITE(VARINT(mintingOutputsStart));
    }
};

struct CSmartContractMessage {
    std::string name;
    CAccounts accounts;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(name);
        READWRITE(accounts);
    }
};

struct CFutureSwapMessage {
    CScript owner;
    CTokenAmount source{};
    uint32_t destination{};
    bool withdraw{};

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(owner);
        READWRITE(source);
        READWRITE(destination);
        READWRITE(withdraw);
    }
};

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
    struct ByBalanceKey { static constexpr uint8_t prefix() { return 'a'; } };
    struct ByHeightKey  { static constexpr uint8_t prefix() { return 'b'; } };

private:
    Res SetBalance(CScript const & owner, CTokenAmount amount);
};

#endif //DEFI_MASTERNODES_ACCOUNTS_H
