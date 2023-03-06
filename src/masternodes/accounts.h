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

struct CFuturesUserKey {
    uint32_t height;
    CScript owner;
    uint32_t txn;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(height));
            height = ~height;
            READWRITE(owner);
            READWRITE(WrapBigEndian(txn));
            txn = ~txn;
        } else {
            uint32_t height_ = ~height;
            READWRITE(WrapBigEndian(height_));
            READWRITE(owner);
            uint32_t txn_ = ~txn;
            READWRITE(WrapBigEndian(txn_));
        }
    }

    bool operator<(const CFuturesUserKey &o) const {
        return std::tie(height, owner, txn) < std::tie(o.height, o.owner, o.txn);
    }
};

struct CFuturesUserValue {
    CTokenAmount source{};
    uint32_t destination{};

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(source);
        READWRITE(destination);
    }
};

class CAccountsView : public virtual CStorageView {
public:
    void ForEachAccount(std::function<bool(const CScript &)> callback, const CScript &start = {});
    void ForEachBalance(std::function<bool(const CScript &, const CTokenAmount &)> callback,
                        const BalanceKey &start = {});
    CTokenAmount GetBalance(const CScript &owner, DCT_ID tokenID) const;

    virtual Res AddBalance(const CScript &owner, CTokenAmount amount);
    virtual Res SubBalance(const CScript &owner, CTokenAmount amount);

    Res AddBalances(const CScript &owner, const CBalances &balances);
    Res SubBalances(const CScript &owner, const CBalances &balances);

    uint32_t GetBalancesHeight(const CScript &owner);
    Res UpdateBalancesHeight(const CScript &owner, uint32_t height);

    Res StoreFuturesUserValues(const CFuturesUserKey &key, const CFuturesUserValue &futures);
    Res EraseFuturesUserValues(const CFuturesUserKey &key);
    void ForEachFuturesUserValues(std::function<bool(const CFuturesUserKey &, const CFuturesUserValue &)> callback,
                                  const CFuturesUserKey &start = {std::numeric_limits<uint32_t>::max(),
                                                                  {},
                                                                  std::numeric_limits<uint32_t>::max()});

    Res StoreFuturesDUSD(const CFuturesUserKey &key, const CAmount &amount);
    Res EraseFuturesDUSD(const CFuturesUserKey &key);
    void ForEachFuturesDUSD(std::function<bool(const CFuturesUserKey &, const CAmount &)> callback,
                            const CFuturesUserKey &start = {std::numeric_limits<uint32_t>::max(),
                                                            {},
                                                            std::numeric_limits<uint32_t>::max()});

    // tags
    struct ByBalanceKey {
        static constexpr uint8_t prefix() { return 'a'; }
    };
    struct ByHeightKey {
        static constexpr uint8_t prefix() { return 'b'; }
    };
    struct ByFuturesSwapKey {
        static constexpr uint8_t prefix() { return 'J'; }
    };
    struct ByFuturesDUSDKey {
        static constexpr uint8_t prefix() { return 'm'; }
    };

private:
    Res SetBalance(const CScript &owner, CTokenAmount amount);
};

#endif  // DEFI_MASTERNODES_ACCOUNTS_H
