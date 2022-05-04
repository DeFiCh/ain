// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_FUTURESWAP_H
#define DEFI_MASTERNODES_FUTURESWAP_H

#include <masternodes/res.h>
#include <masternodes/undos.h>

#include <amount.h>
#include <flushablestorage.h>
#include <script/script.h>

struct CFuturesUserKey {
    uint32_t height{};
    CScript owner;
    uint32_t txn{};

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
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

    bool operator<(const CFuturesUserKey& o) const {
        return std::tie(height, owner, txn) < std::tie(o.height, o.owner, o.txn);
    }
};

struct CFuturesUserValue {
    CTokenAmount source{};
    uint32_t destination{};

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(source);
        READWRITE(destination);
    }
};

class CFutureBaseView : public virtual CStorageView
{
public:
    CFutureBaseView() = default;
    CFutureBaseView(CFutureBaseView& other) = default;

    Res EraseFuturesUserValues(const CFuturesUserKey& key);
    void ForEachFuturesUserValues(std::function<bool(const CFuturesUserKey&, const CFuturesUserValue&)> callback, const CFuturesUserKey& start =
            {std::numeric_limits<uint32_t>::max(), {}, std::numeric_limits<uint32_t>::max()});

    // tags
    struct ByFuturesSwapKey  { static constexpr uint8_t prefix() { return 'J'; } };
};

class CFutureSwapView : public CFutureBaseView
{
public:
    explicit CFutureSwapView(std::shared_ptr<CStorageKV> st) : CStorageView(st) {}

    Res StoreFuturesUserValues(const CFuturesUserKey& key, const CFuturesUserValue& futures);
    ResVal<CFuturesUserValue> GetFuturesUserValues(const CFuturesUserKey& key);
};

extern std::unique_ptr<CFutureSwapView> pfutureSwapView;

#endif //DEFI_MASTERNODES_FUTURESWAP_H
