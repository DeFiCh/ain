// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/accountshistory.h>
#include <masternodes/historywriter.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_checks.h>
#include <masternodes/vaulthistory.h>

extern std::string ScriptToString(const CScript &script);


CHistoryWriters::CHistoryWriters(CAccountHistoryStorage *historyView,
                                 CBurnHistoryStorage *burnView,
                                 CVaultHistoryStorage *vaultView)
        : historyView(historyView),
          burnView(burnView),
          vaultView(vaultView) {}



void CHistoryWriters::AddBalance(const CScript &owner, const CTokenAmount amount, const uint256 &vaultID) {
    if (historyView) {
        diffs[owner][amount.nTokenId] += amount.nValue;
    }
    if (burnView && owner == Params().GetConsensus().burnAddress) {
        burnDiffs[owner][amount.nTokenId] += amount.nValue;
    }
    if (vaultView && !vaultID.IsNull()) {
        vaultDiffs[vaultID][owner][amount.nTokenId] += amount.nValue;
    }
}

void CHistoryWriters::AddFeeBurn(const CScript &owner, const CAmount amount) {
    if (burnView && amount != 0) {
        burnDiffs[owner][DCT_ID{0}] += amount;
    }
}

void CHistoryWriters::SubBalance(const CScript &owner, const CTokenAmount amount, const uint256 &vaultID) {
    if (historyView) {
        diffs[owner][amount.nTokenId] -= amount.nValue;
    }
    if (burnView && owner == Params().GetConsensus().burnAddress) {
        burnDiffs[owner][amount.nTokenId] -= amount.nValue;
    }
    if (vaultView && !vaultID.IsNull()) {
        vaultDiffs[vaultID][owner][amount.nTokenId] -= amount.nValue;
    }
}

void CHistoryWriters::Flush(const uint32_t height,
                            const uint256 &txid,
                            const uint32_t txn,
                            const uint8_t type,
                            const uint256 &vaultID) {
    if (historyView) {
        for (const auto& [owner, amounts] : diffs) {
            LogPrint(BCLog::ACCOUNTCHANGE,
                     "AccountChange: hash=%s type=%s addr=%s change=%s\n",
                     txid.GetHex(),
                     ToString(static_cast<CustomTxType>(type)),
                     ScriptToString(owner),
                     (CBalances{amounts}.ToString()));
            historyView->WriteAccountHistory({owner, height, txn}, {txid, type, amounts});
        }
    }
    if (burnView) {
        for (const auto& [owner, amounts] : burnDiffs) {
            burnView->WriteAccountHistory({owner, height, txn}, {txid, type, amounts});
        }
    }
    if (vaultView) {
        for (const auto& [vaultID, ownerMap] : vaultDiffs) {
            for (const auto& [owner, amounts] : ownerMap) {
                vaultView->WriteVaultHistory({height, vaultID, txn, owner},
                                             {txid, type, amounts});
            }
        }
        if (!schemeID.empty()) {
            vaultView->WriteVaultScheme({vaultID, height}, {type, txid, schemeID, txn});
        }
        if (!globalLoanScheme.identifier.empty()) {
            vaultView->WriteGlobalScheme({height, txn, globalLoanScheme.schemeCreationTxid},
                                         {globalLoanScheme, type, txid});
        }
    }

    // Wipe state after flushing
    ClearState();
}

void CHistoryWriters::ClearState() {
    burnDiffs.clear();
    diffs.clear();
    globalLoanScheme.identifier.clear();
    schemeID.clear();
    vaultDiffs.clear();
}

void CHistoryWriters::EraseHistory(uint32_t height, std::vector<AccountHistoryKey> &eraseBurnEntries) {
    if (historyView) {
        historyView->EraseAccountHistoryHeight(height);
    }

    if (height >= static_cast<uint32_t>(Params().GetConsensus().FortCanningHeight)) {
        // erase auction fee history
        if (historyView) {
            historyView->EraseAuctionHistoryHeight(height);
        }
        if (vaultView) {
            vaultView->EraseVaultHistory(height);
        }
    }

    // Remove burn balance transfers
    if (burnView) {
        burnView->EraseAccountHistoryHeight(height);

        // Erase any UTXO burns
        for (const auto& entries : eraseBurnEntries) {
            burnView->EraseAccountHistory(entries);
        }

        if (height == static_cast<uint32_t>(Params().GetConsensus().EunosHeight)) {
            // Make sure to initialize lastTxOut, otherwise it never finds the block and
            // ends up looping through uninitialized garbage value.
            uint32_t firstTxOut{}, lastTxOut{};
            auto shouldContinueToNextAccountHistory = [&](AccountHistoryKey const & key, AccountHistoryValue const &) -> bool
            {
                if (key.owner != Params().GetConsensus().burnAddress || key.blockHeight != static_cast<uint32_t>(Params().GetConsensus().EunosHeight)) {
                    return false;
                }

                firstTxOut = key.txn;
                if (!lastTxOut) {
                    lastTxOut = key.txn;
                }

                return true;
            };

            AccountHistoryKey startKey({Params().GetConsensus().burnAddress, static_cast<uint32_t>(Params().GetConsensus().EunosHeight), std::numeric_limits<uint32_t>::max()});
            burnView->ForEachAccountHistory(shouldContinueToNextAccountHistory,
                                            Params().GetConsensus().burnAddress,
                                            Params().GetConsensus().EunosHeight);

            for (auto i = firstTxOut; i <= lastTxOut; ++i) {
                burnView->EraseAccountHistory({Params().GetConsensus().burnAddress, static_cast<uint32_t>(Params().GetConsensus().EunosHeight), i});
            }
        }
    }
}

CBurnHistoryStorage*& CHistoryWriters::GetBurnView() {
    return burnView;
}

CVaultHistoryStorage*& CHistoryWriters::GetVaultView() {
    return vaultView;
}

CAccountHistoryStorage*& CHistoryWriters::GetHistoryView() {
    return historyView;
}

void CHistoryWriters::WriteAccountHistory(const AccountHistoryKey &key, const AccountHistoryValue &value) {
    if (burnView) {
        burnView->WriteAccountHistory(key, value);
    }
}

void CHistoryWriters::WriteAuctionHistory(const AuctionHistoryKey &key, const AuctionHistoryValue &value) {
    if (historyView) {
        historyView->WriteAuctionHistory(key, value);
    }
}

void CHistoryWriters::WriteVaultHistory(const VaultHistoryKey &key, const VaultHistoryValue &value) {
    if (vaultView) {
        vaultView->WriteVaultHistory(key, value);
    }
}

void CHistoryWriters::WriteVaultState(CCustomCSView &mnview, const CBlockIndex &pindex, const uint256 &vaultID, const uint32_t ratio) {
    if (vaultView) {
        vaultView->WriteVaultState(mnview, pindex, vaultID, ratio);
    }
}

void CHistoryWriters::FlushDB() {
    if (historyView) {
        historyView->Flush();
    }
    if (burnView) {
        burnView->Flush();
    }
    if (vaultView) {
        vaultView->Flush();
    }
}

void CHistoryWriters::DiscardDB() {
    if (historyView) {
        historyView->Discard();
    }
    if (burnView) {
        burnView->Discard();
    }
    if (vaultView) {
        vaultView->Discard();
    }
}
