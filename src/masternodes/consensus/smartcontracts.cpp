// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <masternodes/accounts.h>
#include <masternodes/consensus/smartcontracts.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/gv.h>
#include <masternodes/masternodes.h>
#include <masternodes/tokens.h>

Res CSmartContractsConsensus::HandleDFIP2201Contract(const CSmartContractMessage& obj) const {
    auto attributes = mnview.GetAttributes();
    if (!attributes)
        return Res::Err("Attributes unavailable");

    CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::Active};

    if (!attributes->GetValue(activeKey, false))
        return Res::Err("DFIP2201 smart contract is not enabled");

    if (obj.name != SMART_CONTRACT_DFIP_2201)
        return Res::Err("DFIP2201 contract mismatch - got: " + obj.name);

    if (obj.accounts.size() != 1)
        return Res::Err("Only one address entry expected for " + obj.name);

    if (obj.accounts.begin()->second.balances.size() != 1)
        return Res::Err("Only one amount entry expected for " + obj.name);

    const auto& script = obj.accounts.begin()->first;
    if (!HasAuth(script))
        return Res::Err("Must have at least one input from supplied address");

    const auto& id = obj.accounts.begin()->second.balances.begin()->first;
    const auto& amount = obj.accounts.begin()->second.balances.begin()->second;

    if (amount <= 0)
        return Res::Err("Amount out of range");

    CDataStructureV0 minSwapKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::MinSwap};
    auto minSwap = attributes->GetValue(minSwapKey, CAmount{0});

    if (minSwap && amount < minSwap)
        return Res::Err("Below minimum swapable amount, must be at least " + GetDecimaleString(minSwap) + " BTC");

    const auto token = mnview.GetToken(id);
    if (!token)
        return Res::Err("Specified token not found");

    if (token->symbol != "BTC" || token->name != "Bitcoin" || !token->IsDAT())
        return Res::Err("Only Bitcoin can be swapped in " + obj.name);

    auto res = mnview.SubBalance(script, {id, amount});
    if (!res)
        return res;

    const CTokenCurrencyPair btcUsd{"BTC","USD"};
    const CTokenCurrencyPair dfiUsd{"DFI","USD"};

    bool useNextPrice{false}, requireLivePrice{true};
    auto resVal = mnview.GetValidatedIntervalPrice(btcUsd, useNextPrice, requireLivePrice);
    if (!resVal)
        return std::move(resVal);

    CDataStructureV0 premiumKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::Premium};
    auto premium = attributes->GetValue(premiumKey, CAmount{2500000});

    const auto& btcPrice = MultiplyAmounts(*resVal.val, premium + COIN);

    resVal = mnview.GetValidatedIntervalPrice(dfiUsd, useNextPrice, requireLivePrice);
    if (!resVal)
        return std::move(resVal);

    const auto totalDFI = MultiplyAmounts(DivideAmounts(btcPrice, *resVal.val), amount);
    res = mnview.SubBalance(consensus.smartContracts.begin()->second, {{0}, totalDFI});
    return !res ? res : mnview.AddBalance(script, {{0}, totalDFI});
}

Res CSmartContractsConsensus::operator()(const CFutureSwapMessage& obj) const {
    if (!HasAuth(obj.owner))
        return Res::Err("Transaction must have at least one input from owner");

    const auto attributes = mnview.GetAttributes();
    if (!attributes)
        return Res::Err("Attributes unavailable");

    CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::Active};
    const auto active = attributes->GetValue(activeKey, false);
    if (!active)
        return Res::Err("DFIP2203 not currently active");

    CDataStructureV0 blockKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::BlockPeriod};
    CDataStructureV0 rewardKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::RewardPct};
    if (!attributes->CheckKey(blockKey) || !attributes->CheckKey(rewardKey))
        return Res::Err("DFIP2203 not currently active");

    if (obj.source.nValue <= 0)
        return Res::Err("Source amount must be more than zero");

    const auto source = mnview.GetLoanTokenByID(obj.source.nTokenId);
    if (!source)
        return Res::Err("Could not get source loan token %d", obj.source.nTokenId.v);

    uint32_t tokenId;

    if (source->symbol == "DUSD") {
        tokenId = obj.destination;

        if (!mnview.GetLoanTokenByID({tokenId}))
            return Res::Err("Could not get destination loan token %d. Set valid destination.", tokenId);
    } else {
        if (obj.destination != 0)
            return Res::Err("Destination should not be set when source amount is a dToken");

        tokenId = obj.source.nTokenId.v;
    }

    CDataStructureV0 tokenKey{AttributeTypes::Token, tokenId, TokenKeys::DFIP2203Enabled};
    if (!attributes->GetValue(tokenKey, true))
        return Res::Err("DFIP2203 currently disabled for token %d", tokenId);

    const auto contractAddressValue = GetFutureSwapContractAddress();
    if (!contractAddressValue)
        return contractAddressValue;

    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2203Current};
    auto balances = attributes->GetValue(liveKey, CBalances{});

    if (obj.withdraw) {
        std::map<CFuturesUserKey, CFuturesUserValue> userFuturesValues;

        futureSwapView.ForEachFuturesUserValues([&](const CFuturesUserKey& key, const CFuturesUserValue& futuresValues) {
            if (key.owner == obj.owner &&
                futuresValues.source.nTokenId == obj.source.nTokenId &&
                futuresValues.destination == obj.destination) {
                userFuturesValues[key] = futuresValues;
            }

            return true;
        }, {height, obj.owner, std::numeric_limits<uint32_t>::max()});

        CTokenAmount totalFutures{};
        totalFutures.nTokenId = obj.source.nTokenId;

        for (const auto& [key, value] : userFuturesValues) {
            totalFutures.Add(value.source.nValue);
            futureSwapView.EraseFuturesUserValues(key);
        }

        auto res = totalFutures.Sub(obj.source.nValue);
        if (!res)
            return res;

        if (totalFutures.nValue > 0) {
            auto res = futureSwapView.StoreFuturesUserValues({height, obj.owner, txn}, {totalFutures, obj.destination});
            if (!res)
                return res;
        }

        res = TransferTokenBalance(obj.source.nTokenId, obj.source.nValue, *contractAddressValue, obj.owner);
        if (!res)
            return res;

        res = balances.Sub(obj.source);
        if (!res)
            return res;
    } else {
        auto res = TransferTokenBalance(obj.source.nTokenId, obj.source.nValue, obj.owner, *contractAddressValue);
        if (!res)
            return res;

        res = futureSwapView.StoreFuturesUserValues({height, obj.owner, txn}, {obj.source, obj.destination});
        if (!res)
            return res;

        balances.Add(obj.source);
    }

    attributes->SetValue(liveKey, std::move(balances));
    return mnview.SetVariable(*attributes);
}

Res CSmartContractsConsensus::operator()(const CSmartContractMessage& obj) const {
    if (obj.accounts.empty())
        return Res::Err("Contract account parameters missing");

    auto contracts = consensus.smartContracts;
    auto contract = contracts.find(obj.name);
    if (contract == contracts.end())
        return Res::Err("Specified smart contract not found");

    // Convert to switch when it's long enough.
    if (obj.name == SMART_CONTRACT_DFIP_2201)
        return HandleDFIP2201Contract(obj);

    return Res::Err("Specified smart contract not found");
}
