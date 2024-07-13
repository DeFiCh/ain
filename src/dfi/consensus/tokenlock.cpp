// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/params.h>
#include <dfi/accountshistory.h>
#include <dfi/consensus/tokenlock.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/historywriter.h>
#include <dfi/masternodes.h>
#include <dfi/mn_checks.h>

Res CTokenLockConsensus::operator()(const CReleaseLockMessage &obj) const {
    if (!HasFoundationAuth()) {
        return Res::Err("tx not from foundation member");
    }
    if (obj.releasePart == 0) {
        return Res::Err("release ratio can not be 0");
    }
    const auto &tx = txCtx.GetTransaction();
    auto &mnview = blockCtx.GetView();

    // get current ratio from attributes
    auto attributes = mnview.GetAttributes();

    CDataStructureV0 releaseKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::TokenLockRatio};
    auto currentRatio = attributes->GetValue(releaseKey, CAmount{});
    if (currentRatio < obj.releasePart) {
        return Res::Err("can't release more than locked %s vs %s",
                        GetDecimalString(currentRatio),
                        GetDecimalString(obj.releasePart));
    }

    auto newRatio = currentRatio - obj.releasePart;
    // part of locked funds to be released
    auto releaseAmount = [&](const CAmount &amount) {
        if (currentRatio <= obj.releasePart) {
            return amount;
        } else {
            return MultiplyDivideAmounts(amount, obj.releasePart, currentRatio);
        }
    };
    LogPrintf("releasing locked tokens, current ratio %s, releasing %s, resulting ratio %s. \n",
              GetDecimalString(currentRatio),
              GetDecimalString(obj.releasePart),
              GetDecimalString(newRatio));

    // calc part of current funds that should be released
    // cap to all funds
    // for each tokenlock: release funds and update values
    const auto contractAddressValue = blockCtx.GetConsensus().smartContracts.at(SMART_CONTRACT_TOKENLOCK);
    std::vector<CTokenLockUserKey> todelete{};
    mnview.ForEachTokenLockUserValues([&](const auto &key, const auto &value) {
        const auto &owner = key.owner;
        auto newBalance = CTokenLockUserValue{};
        bool gotNew = false;
        CAccountsHistoryWriter view(
            mnview, blockCtx.GetHeight(), txCtx.GetTxn(), tx.GetHash(), uint8_t(CustomTxType::TokenLockRelease));

        for (const auto &[tokenId, amount] : value.balances) {
            const CTokenAmount moved = {tokenId, releaseAmount(amount)};

            view.AddBalance(owner, moved);
            view.SubBalance(contractAddressValue, moved);
            auto updated = amount - moved.nValue;
            if (updated > 0) {
                newBalance.Add({tokenId, updated});
                gotNew = true;
            }
        }
        view.Flush();
        if (gotNew) {
            mnview.StoreTokenLockUserValues(key, newBalance);
        } else {
            todelete.emplace_back(key);
        }
        return true;
    });
    attributes->SetValue(releaseKey, newRatio);
    mnview.SetVariable(*attributes);
    for (const auto &key : todelete) {
        mnview.EraseTokenLockUserValues(key);
    }

    return Res::Ok();
}