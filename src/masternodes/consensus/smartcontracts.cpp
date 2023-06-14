// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <masternodes/accounts.h>
#include <masternodes/consensus/smartcontracts.h>
#include <masternodes/errors.h>
#include <masternodes/govvariables/attributes.h>
#include <masternodes/masternodes.h>


Res CSmartContractsConsensus::HandleDFIP2201Contract(const CSmartContractMessage &obj) const {
    const auto attributes = mnview.GetAttributes();
    Require(attributes, "Attributes unavailable");

    CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::Active};

    Require(attributes->GetValue(activeKey, false), "DFIP2201 smart contract is not enabled");

    Require(obj.name == SMART_CONTRACT_DFIP_2201, "DFIP2201 contract mismatch - got: " + obj.name);

    Require(obj.accounts.size() == 1, "Only one address entry expected for " + obj.name);

    Require(obj.accounts.begin()->second.balances.size() == 1, "Only one amount entry expected for " + obj.name);

    const auto &script = obj.accounts.begin()->first;
    Require(HasAuth(script), "Must have at least one input from supplied address");

    const auto &id     = obj.accounts.begin()->second.balances.begin()->first;
    const auto &amount = obj.accounts.begin()->second.balances.begin()->second;

    Require(amount > 0, "Amount out of range");

    CDataStructureV0 minSwapKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::MinSwap};
    auto minSwap = attributes->GetValue(minSwapKey, CAmount{0});

    if (amount < minSwap) {
        return DeFiErrors::ICXBTCBelowMinSwap(amount, minSwap);
    }

    const auto token = mnview.GetToken(id);
    Require(token, "Specified token not found");

    Require(token->symbol == "BTC" && token->name == "Bitcoin" && token->IsDAT(),
            "Only Bitcoin can be swapped in " + obj.name);

    if (height >= static_cast<uint32_t>(consensus.NextNetworkUpgradeHeight)) {
        mnview.CalculateOwnerRewards(script, height);
    }

    Require(mnview.SubBalance(script, {id, amount}));

    const CTokenCurrencyPair btcUsd{"BTC", "USD"};
    const CTokenCurrencyPair dfiUsd{"DFI", "USD"};

    bool useNextPrice{false}, requireLivePrice{true};
    auto resVal = mnview.GetValidatedIntervalPrice(btcUsd, useNextPrice, requireLivePrice);
    Require(resVal);

    CDataStructureV0 premiumKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::Premium};
    auto premium = attributes->GetValue(premiumKey, CAmount{2500000});

    const auto &btcPrice = MultiplyAmounts(*resVal.val, premium + COIN);

    resVal = mnview.GetValidatedIntervalPrice(dfiUsd, useNextPrice, requireLivePrice);
    Require(resVal);

    const auto totalDFI = MultiplyAmounts(DivideAmounts(btcPrice, *resVal.val), amount);

    Require(mnview.SubBalance(Params().GetConsensus().smartContracts.begin()->second, {{0}, totalDFI}));

    Require(mnview.AddBalance(script, {{0}, totalDFI}));

    return Res::Ok();
}

Res CSmartContractsConsensus::operator()(const CSmartContractMessage &obj) const {
    Require(!obj.accounts.empty(), "Contract account parameters missing");
    auto contracts = Params().GetConsensus().smartContracts;

    auto contract = contracts.find(obj.name);
    Require(contract != contracts.end(), "Specified smart contract not found");

    // Convert to switch when it's long enough.
    if (obj.name == SMART_CONTRACT_DFIP_2201)
        return HandleDFIP2201Contract(obj);

    return Res::Err("Specified smart contract not found");
}

Res CSmartContractsConsensus::operator()(const CFutureSwapMessage &obj) const {
    LogPrintf("XXX TX hash %s\n", tx.GetHash().ToString());
    Require(HasAuth(obj.owner), "Transaction must have at least one input from owner");

    const auto attributes = mnview.GetAttributes();
    Require(attributes, "Attributes unavailable");

    bool dfiToDUSD     = !obj.source.nTokenId.v;
    const auto paramID = dfiToDUSD ? ParamIDs::DFIP2206F : ParamIDs::DFIP2203;

    CDataStructureV0 activeKey{AttributeTypes::Param, paramID, DFIPKeys::Active};
    CDataStructureV0 blockKey{AttributeTypes::Param, paramID, DFIPKeys::BlockPeriod};
    CDataStructureV0 rewardKey{AttributeTypes::Param, paramID, DFIPKeys::RewardPct};

    Require(
            attributes->GetValue(activeKey, false) && attributes->CheckKey(blockKey) && attributes->CheckKey(rewardKey),
            "%s not currently active",
            dfiToDUSD ? "DFIP2206F" : "DFIP2203");

    CDataStructureV0 startKey{AttributeTypes::Param, paramID, DFIPKeys::StartBlock};
    if (const auto startBlock = attributes->GetValue(startKey, CAmount{})) {
        Require(
                height >= startBlock, "%s not active until block %d", dfiToDUSD ? "DFIP2206F" : "DFIP2203", startBlock);
    }

    Require(obj.source.nValue > 0, "Source amount must be more than zero");

    const auto source = mnview.GetLoanTokenByID(obj.source.nTokenId);
    Require(dfiToDUSD || source, "Could not get source loan token %d", obj.source.nTokenId.v);

    if (!dfiToDUSD && source->symbol == "DUSD") {
        CDataStructureV0 tokenKey{AttributeTypes::Token, obj.destination, TokenKeys::DFIP2203Enabled};
        const auto enabled = attributes->GetValue(tokenKey, true);
        Require(enabled, "DFIP2203 currently disabled for token %d", obj.destination);

        const auto loanToken = mnview.GetLoanTokenByID({obj.destination});
        Require(loanToken, "Could not get destination loan token %d. Set valid destination.", obj.destination);

        Require(!mnview.AreTokensLocked({obj.destination}), "Cannot create future swap for locked token");
    } else {
        if (!dfiToDUSD) {
            Require(obj.destination == 0, "Destination should not be set when source amount is dToken or DFI");

            Require(!mnview.AreTokensLocked({obj.source.nTokenId.v}), "Cannot create future swap for locked token");

            CDataStructureV0 tokenKey{AttributeTypes::Token, obj.source.nTokenId.v, TokenKeys::DFIP2203Enabled};
            const auto enabled = attributes->GetValue(tokenKey, true);
            Require(enabled, "DFIP2203 currently disabled for token %s", obj.source.nTokenId.ToString());
        } else {
            DCT_ID id{};
            const auto token = mnview.GetTokenGuessId("DUSD", id);
            Require(token, "No DUSD token defined");

            Require(mnview.GetFixedIntervalPrice({"DFI", "USD"}), "DFI / DUSD fixed interval price not found");

            Require(obj.destination == id.v,
                    "Incorrect destination defined for DFI swap, DUSD destination expected id: %d",
                    id.v);
        }
    }

    const auto contractType         = dfiToDUSD ? SMART_CONTRACT_DFIP2206F : SMART_CONTRACT_DFIP_2203;
    const auto contractAddressValue = GetFutureSwapContractAddress(contractType);
    Require(contractAddressValue);

    const auto economyKey = dfiToDUSD ? EconomyKeys::DFIP2206FCurrent : EconomyKeys::DFIP2203Current;
    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, economyKey};
    auto balances = attributes->GetValue(liveKey, CBalances{});

    if (height >= static_cast<uint32_t>(consensus.FortCanningCrunchHeight)) {
        CalculateOwnerRewards(obj.owner);
    }

    if (obj.withdraw) {
        CTokenAmount totalFutures{};
        totalFutures.nTokenId = obj.source.nTokenId;

        if (!dfiToDUSD) {
            std::map<CFuturesUserKey, CFuturesUserValue> userFuturesValues;

            mnview.ForEachFuturesUserValues(
                    [&](const CFuturesUserKey &key, const CFuturesUserValue &futuresValues) {
                        if (key.owner == obj.owner && futuresValues.source.nTokenId == obj.source.nTokenId &&
                            futuresValues.destination == obj.destination) {
                            userFuturesValues[key] = futuresValues;
                        }
                        return true;
                    },
                    {height, obj.owner, std::numeric_limits<uint32_t>::max()});

            for (const auto &[key, value] : userFuturesValues) {
                totalFutures.Add(value.source.nValue);
                mnview.EraseFuturesUserValues(key);
            }
        } else {
            std::map<CFuturesUserKey, CAmount> userFuturesValues;

            mnview.ForEachFuturesDUSD(
                    [&](const CFuturesUserKey &key, const CAmount &futuresValues) {
                        if (key.owner == obj.owner) {
                            userFuturesValues[key] = futuresValues;
                        }
                        return true;
                    },
                    {height, obj.owner, std::numeric_limits<uint32_t>::max()});

            for (const auto &[key, amount] : userFuturesValues) {
                totalFutures.Add(amount);
                mnview.EraseFuturesDUSD(key);
            }
        }

        Require(totalFutures.Sub(obj.source.nValue));

        if (totalFutures.nValue > 0) {
            Res res{};
            if (!dfiToDUSD) {
                Require(mnview.StoreFuturesUserValues({height, obj.owner, txn}, {totalFutures, obj.destination}));
            } else {
                Require(mnview.StoreFuturesDUSD({height, obj.owner, txn}, totalFutures.nValue));
            }
        }

        Require(TransferTokenBalance(obj.source.nTokenId, obj.source.nValue, *contractAddressValue, obj.owner));

        Require(balances.Sub(obj.source));
    } else {
        Require(TransferTokenBalance(obj.source.nTokenId, obj.source.nValue, obj.owner, *contractAddressValue));

        if (!dfiToDUSD) {
            Require(mnview.StoreFuturesUserValues({height, obj.owner, txn}, {obj.source, obj.destination}));
        } else {
            Require(mnview.StoreFuturesDUSD({height, obj.owner, txn}, obj.source.nValue));
        }
        balances.Add(obj.source);
    }

    attributes->SetValue(liveKey, balances);

    mnview.SetVariable(*attributes);

    return Res::Ok();
}
