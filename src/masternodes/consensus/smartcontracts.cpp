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
    Require(attributes, "Attributes unavailable");

    CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::Active};
    Require(attributes->GetValue(activeKey, false), "DFIP2201 smart contract is not enabled");
    Require(obj.name == SMART_CONTRACT_DFIP_2201, "DFIP2201 contract mismatch - got: " + obj.name);
    Require(obj.accounts.size() == 1, "Only one address entry expected for " + obj.name);
    Require(obj.accounts.begin()->second.balances.size() == 1, "Only one amount entry expected for " + obj.name);

    const auto& script = obj.accounts.begin()->first;
    Require(HasAuth(script));

    const auto& id = obj.accounts.begin()->second.balances.begin()->first;
    const auto& amount = obj.accounts.begin()->second.balances.begin()->second;
    Require(amount > 0, "Amount out of range");

    CDataStructureV0 minSwapKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::MinSwap};
    auto minSwap = attributes->GetValue(minSwapKey, CAmount{0});
    Require(amount >= minSwap, "Below minimum swapable amount, must be at least " + GetDecimaleString(minSwap) + " BTC");

    auto token = mnview.GetToken(id);
    Require(token, "Specified token not found");
    Require(token->symbol == "BTC" && token->name == "Bitcoin" && token->IsDAT(), "Only Bitcoin can be swapped in " + obj.name);

    Require(mnview.SubBalance(script, {id, amount}));

    const CTokenCurrencyPair btcUsd{"BTC","USD"};
    const CTokenCurrencyPair dfiUsd{"DFI","USD"};

    bool useNextPrice{false}, requireLivePrice{true};
    auto BtcUsd = mnview.GetValidatedIntervalPrice(btcUsd, useNextPrice, requireLivePrice);
    Require(BtcUsd);

    CDataStructureV0 premiumKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::Premium};
    auto premium = attributes->GetValue(premiumKey, CAmount{2500000});

    const auto& btcPrice = MultiplyAmounts(*BtcUsd, premium + COIN);

    auto DfiUsd = mnview.GetValidatedIntervalPrice(dfiUsd, useNextPrice, requireLivePrice);
    Require(DfiUsd);

    const auto totalDFI = MultiplyAmounts(DivideAmounts(btcPrice, *DfiUsd), amount);
    Require(mnview.SubBalance(consensus.smartContracts.begin()->second, {{0}, totalDFI}));
    return mnview.AddBalance(script, {{0}, totalDFI});
}

Res CSmartContractsConsensus::operator()(const CFutureSwapMessage& obj) const {
    Require(HasAuth(obj.owner));

    const auto attributes = mnview.GetAttributes();
    Require(attributes, "Attributes unavailable");

    CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::Active};
    const auto active = attributes->GetValue(activeKey, false);
    Require(active, "DFIP2203 not currently active");

    CDataStructureV0 blockKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::BlockPeriod};
    CDataStructureV0 rewardKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::RewardPct};
    Require(attributes->CheckKey(blockKey) && attributes->CheckKey(rewardKey), "DFIP2203 not currently active");

    CDataStructureV0 startKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::StartBlock};
    if (const auto startBlock = attributes->GetValue(startKey, CAmount{}); height < startBlock) {
        return Res::Err("DFIP2203 not active until block %d", startBlock);
    }

    Require(obj.source.nValue > 0, "Source amount must be more than zero");

    const auto source = mnview.GetLoanTokenByID(obj.source.nTokenId);
    Require(source, "Could not get source loan token %d", obj.source.nTokenId.v);

    uint32_t tokenId;

    if (source->symbol == "DUSD") {
        tokenId = obj.destination;
        Require(mnview.GetLoanTokenByID({tokenId}), "Could not get destination loan token %d. Set valid destination.", tokenId);
    } else {
        Require(obj.destination == 0, "Destination should not be set when source amount is a dToken");
        tokenId = obj.source.nTokenId.v;
    }

    CDataStructureV0 tokenKey{AttributeTypes::Token, tokenId, TokenKeys::DFIP2203Enabled};
    Require(attributes->GetValue(tokenKey, true), "DFIP2203 currently disabled for token %d", tokenId);

    const auto contractAddressValue = GetFutureSwapContractAddress();
    Require(contractAddressValue);

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

        Require(totalFutures.Sub(obj.source.nValue));

        if (totalFutures.nValue > 0) {
            Require(futureSwapView.StoreFuturesUserValues({height, obj.owner, txn}, {totalFutures, obj.destination}));
        }

        Require(TransferTokenBalance(obj.source.nTokenId, obj.source.nValue, *contractAddressValue, obj.owner));
        Require(balances.Sub(obj.source));

    } else {
        // some txs might be rejected due to not enough owner amount
        if (static_cast<int>(height) >= consensus.GreatWorldHeight)
            CalculateOwnerRewards(obj.owner);

        Require(TransferTokenBalance(obj.source.nTokenId, obj.source.nValue, obj.owner, *contractAddressValue));

        Require(futureSwapView.StoreFuturesUserValues({height, obj.owner, txn}, {obj.source, obj.destination}));

        balances.Add(obj.source);
    }

    attributes->SetValue(liveKey, std::move(balances));
    return mnview.SetVariable(*attributes);
}

Res CSmartContractsConsensus::operator()(const CSmartContractMessage& obj) const {
    Require(!obj.accounts.empty(), "Contract account parameters missing");

    auto contracts = consensus.smartContracts;
    auto contract = contracts.find(obj.name);
    Require(contract != contracts.end(), "Specified smart contract not found");

    // Convert to switch when it's long enough.
    if (obj.name == SMART_CONTRACT_DFIP_2201)
        return HandleDFIP2201Contract(obj);

    return Res::Err("Specified smart contract not found");
}
