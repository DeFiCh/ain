// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_HISTORYWRITER_H
#define DEFI_MASTERNODES_HISTORYWRITER_H

#include <amount.h>
#include <masternodes/loan.h>
#include <script/script.h>
#include <uint256.h>

class CAccountHistoryStorage;
struct AuctionHistoryKey;
struct AuctionHistoryValue;
class CBurnHistoryStorage;
class CVaultHistoryStorage;
struct VaultHistoryKey;
struct VaultHistoryValue;

struct AccountHistoryKey {
    CScript owner;
    uint32_t blockHeight;
    uint32_t txn;  // for order in block

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(owner);

        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(blockHeight));
            blockHeight = ~blockHeight;
            READWRITE(WrapBigEndian(txn));
            txn = ~txn;
        } else {
            uint32_t blockHeight_ = ~blockHeight;
            READWRITE(WrapBigEndian(blockHeight_));
            uint32_t txn_ = ~txn;
            READWRITE(WrapBigEndian(txn_));
        }
    }
};

struct AccountHistoryKeyNew {
    uint32_t blockHeight;
    CScript owner;
    uint32_t txn;  // for order in block

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(blockHeight));
            blockHeight = ~blockHeight;
        } else {
            uint32_t blockHeight_ = ~blockHeight;
            READWRITE(WrapBigEndian(blockHeight_));
        }

        READWRITE(owner);

        if (ser_action.ForRead()) {
            READWRITE(WrapBigEndian(txn));
            txn = ~txn;
        } else {
            uint32_t txn_ = ~txn;
            READWRITE(WrapBigEndian(txn_));
        }
    }
};

struct AccountHistoryValue {
    uint256 txid;
    unsigned char category;
    TAmounts diff;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(txid);
        READWRITE(category);
        READWRITE(diff);
    }
};

class CHistoryWriters {
    CAccountHistoryStorage *historyView{};
    CBurnHistoryStorage *burnView{};
    CVaultHistoryStorage *vaultView{};

    std::map<CScript, TAmounts> diffs;
    std::map<CScript, TAmounts> burnDiffs;
    std::map<uint256, std::map<CScript, TAmounts>> vaultDiffs;

public:
    CLoanSchemeCreation globalLoanScheme;
    std::string schemeID;

    CHistoryWriters() = default;
    CHistoryWriters(const CHistoryWriters &writers) = default;
    CHistoryWriters(CAccountHistoryStorage *historyView,
                    CBurnHistoryStorage *burnView,
                    CVaultHistoryStorage *vaultView);

    void AddBalance(const CScript &owner, const CTokenAmount amount, const uint256 &vaultID);
    void AddFeeBurn(const CScript &owner, const CAmount amount);
    void SubBalance(const CScript &owner, const CTokenAmount amount, const uint256 &vaultID);

    void ClearState();
    void FlushDB();
    void DiscardDB();
    void Flush(const uint32_t height,
               const uint256 &txid,
               const uint32_t txn,
               const uint8_t type,
               const uint256 &vaultID);

    CBurnHistoryStorage*& GetBurnView();
    CVaultHistoryStorage*& GetVaultView();
    CAccountHistoryStorage*& GetHistoryView();

    void WriteAccountHistory(const AccountHistoryKey &key, const AccountHistoryValue &value);
    void WriteAuctionHistory(const AuctionHistoryKey &key, const AuctionHistoryValue &value);
    void WriteVaultHistory(const VaultHistoryKey &key, const VaultHistoryValue &value);
    void WriteVaultState(CCustomCSView &mnview, const CBlockIndex &pindex, const uint256 &vaultID, const uint32_t ratio = 0);
    void EraseHistory(uint32_t height, std::vector<AccountHistoryKey> &eraseBurnEntries);
};

#endif  // DEFI_MASTERNODES_HISTORYWRITER_H