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
    const auto attributes = mnview.GetAttributes();
    if (!attributes)
        return Res::Err("Attributes unavailable");

    CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIP2201Keys::Active};

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

    CDataStructureV0 minSwapKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIP2201Keys::MinSwap};
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

    CDataStructureV0 premiumKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIP2201Keys::Premium};
    auto premium = attributes->GetValue(premiumKey, CAmount{2500000});

    const auto& btcPrice = MultiplyAmounts(*resVal.val, premium + COIN);

    resVal = mnview.GetValidatedIntervalPrice(dfiUsd, useNextPrice, requireLivePrice);
    if (!resVal)
        return std::move(resVal);

    const auto totalDFI = MultiplyAmounts(DivideAmounts(btcPrice, *resVal.val), amount);

    res = mnview.SubBalance(Params().GetConsensus().smartContracts.begin()->second, {{0}, totalDFI});
    if (!res)
        return res;

    res = mnview.AddBalance(script, {{0}, totalDFI});
    if (!res)
        return res;

    return Res::Ok();
}

Res CSmartContractsConsensus::operator()(const CSmartContractMessage& obj) const {
    if (obj.accounts.empty())
        return Res::Err("Contract account parameters missing");

    auto contracts = Params().GetConsensus().smartContracts;
    auto contract = contracts.find(obj.name);
    if (contract == contracts.end())
        return Res::Err("Specified smart contract not found");

    // Convert to switch when it's long enough.
    if (obj.name == SMART_CONTRACT_DFIP_2201)
        return HandleDFIP2201Contract(obj);

    return Res::Err("Specified smart contract not found");
}
