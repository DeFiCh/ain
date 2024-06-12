// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <dfi/accounts.h>
#include <dfi/consensus/smartcontracts.h>
#include <dfi/errors.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/masternodes.h>
#include <dfi/mn_checks.h>
#include <dfi/validation.h>

Res CSmartContractsConsensus::HandleDFIP2201Contract(const CSmartContractMessage &obj) const {
    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    auto &mnview = blockCtx.GetView();
    const auto attributes = mnview.GetAttributes();

    CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::Active};

    if (!attributes->GetValue(activeKey, false)) {
        return Res::Err("DFIP2201 smart contract is not enabled");
    }

    if (obj.name != SMART_CONTRACT_DFIP_2201) {
        return Res::Err("DFIP2201 contract mismatch - got: " + obj.name);
    }

    if (obj.accounts.size() != 1) {
        return Res::Err("Only one address entry expected for " + obj.name);
    }

    if (obj.accounts.begin()->second.balances.size() != 1) {
        return Res::Err("Only one amount entry expected for " + obj.name);
    }

    const auto &script = obj.accounts.begin()->first;
    if (!HasAuth(script)) {
        return Res::Err("Must have at least one input from supplied address");
    }

    const auto &id = obj.accounts.begin()->second.balances.begin()->first;
    const auto &amount = obj.accounts.begin()->second.balances.begin()->second;

    if (amount <= 0) {
        return Res::Err("Amount out of range");
    }

    CDataStructureV0 minSwapKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::MinSwap};
    auto minSwap = attributes->GetValue(minSwapKey, CAmount{0});

    if (amount < minSwap) {
        return DeFiErrors::ICXBTCBelowMinSwap(amount, minSwap);
    }

    const auto token = mnview.GetToken(id);
    if (!token) {
        return Res::Err("Specified token not found");
    }

    if (token->symbol != "BTC" || token->name != "Bitcoin" || !token->IsDAT()) {
        return Res::Err("Only Bitcoin can be swapped in " + obj.name);
    }

    if (height >= static_cast<uint32_t>(consensus.DF22MetachainHeight)) {
        mnview.CalculateOwnerRewards(script, height);
    }

    if (auto res = mnview.SubBalance(script, {id, amount}); !res) {
        return res;
    }

    const CTokenCurrencyPair btcUsd{"BTC", "USD"};
    const CTokenCurrencyPair dfiUsd{"DFI", "USD"};

    bool useNextPrice{false}, requireLivePrice{true};
    auto resVal = mnview.GetValidatedIntervalPrice(btcUsd, useNextPrice, requireLivePrice);
    if (!resVal) {
        return resVal;
    }

    CDataStructureV0 premiumKey{AttributeTypes::Param, ParamIDs::DFIP2201, DFIPKeys::Premium};
    auto premium = attributes->GetValue(premiumKey, CAmount{2500000});

    const auto &btcPrice = MultiplyAmounts(*resVal.val, premium + COIN);

    resVal = mnview.GetValidatedIntervalPrice(dfiUsd, useNextPrice, requireLivePrice);
    if (!resVal) {
        return resVal;
    }

    const auto totalDFI = MultiplyAmounts(DivideAmounts(btcPrice, *resVal.val), amount);

    if (auto res = mnview.SubBalance(consensus.smartContracts.begin()->second, {{0}, totalDFI}); !res) {
        return res;
    }

    if (auto res = mnview.AddBalance(script, {{0}, totalDFI}); !res) {
        return res;
    }

    return Res::Ok();
}

Res CSmartContractsConsensus::operator()(const CSmartContractMessage &obj) const {
    if (obj.accounts.empty()) {
        return Res::Err("Contract account parameters missing");
    }

    const auto &consensus = txCtx.GetConsensus();
    auto contracts = consensus.smartContracts;

    auto contract = contracts.find(obj.name);
    if (contract == contracts.end()) {
        return Res::Err("Specified smart contract not found");
    }

    // Convert to switch when it's long enough.
    if (obj.name == SMART_CONTRACT_DFIP_2201) {
        return HandleDFIP2201Contract(obj);
    }

    return Res::Err("Specified smart contract not found");
}

Res CSmartContractsConsensus::operator()(const CFutureSwapMessage &obj) const {
    if (!HasAuth(obj.owner)) {
        return Res::Err("Transaction must have at least one input from owner");
    }

    const auto &consensus = txCtx.GetConsensus();
    const auto height = txCtx.GetHeight();
    const auto txn = txCtx.GetTxn();
    auto &mnview = blockCtx.GetView();
    const auto attributes = mnview.GetAttributes();

    bool dfiToDUSD = !obj.source.nTokenId.v;
    const auto paramID = dfiToDUSD ? ParamIDs::DFIP2206F : ParamIDs::DFIP2203;

    CDataStructureV0 activeKey{AttributeTypes::Param, paramID, DFIPKeys::Active};
    CDataStructureV0 blockKey{AttributeTypes::Param, paramID, DFIPKeys::BlockPeriod};
    CDataStructureV0 rewardKey{AttributeTypes::Param, paramID, DFIPKeys::RewardPct};

    if (!attributes->GetValue(activeKey, false) || !attributes->CheckKey(blockKey) ||
        !attributes->CheckKey(rewardKey)) {
        return Res::Err("%s not currently active", dfiToDUSD ? "DFIP2206F" : "DFIP2203");
    }

    CDataStructureV0 startKey{AttributeTypes::Param, paramID, DFIPKeys::StartBlock};
    if (const auto startBlock = attributes->GetValue(startKey, CAmount{})) {
        if (height < startBlock) {
            return Res::Err("%s not active until block %d", dfiToDUSD ? "DFIP2206F" : "DFIP2203", startBlock);
        }
    }

    if (obj.source.nValue <= 0) {
        return Res::Err("Source amount must be more than zero");
    }

    const auto source = mnview.GetLoanTokenByID(obj.source.nTokenId);
    if (!dfiToDUSD && !source) {
        return Res::Err("Could not get source loan token %d", obj.source.nTokenId.v);
    }

    if (!dfiToDUSD && source->symbol == "DUSD") {
        CDataStructureV0 tokenKey{AttributeTypes::Token, obj.destination, TokenKeys::DFIP2203Enabled};
        const auto enabled = attributes->GetValue(tokenKey, true);
        if (!enabled) {
            return Res::Err("DFIP2203 currently disabled for token %d", obj.destination);
        }

        const auto loanToken = mnview.GetLoanTokenByID({obj.destination});
        if (!loanToken) {
            return Res::Err("Could not get destination loan token %d. Set valid destination.", obj.destination);
        }

        if (mnview.AreTokensLocked({obj.destination})) {
            return Res::Err("Cannot create future swap for locked token");
        }
    } else {
        if (!dfiToDUSD) {
            if (obj.destination != 0) {
                return Res::Err("Destination should not be set when source amount is dToken or DFI");
            }

            if (mnview.AreTokensLocked({obj.source.nTokenId.v})) {
                return Res::Err("Cannot create future swap for locked token");
            }

            CDataStructureV0 tokenKey{AttributeTypes::Token, obj.source.nTokenId.v, TokenKeys::DFIP2203Enabled};
            const auto enabled = attributes->GetValue(tokenKey, true);
            if (!enabled) {
                return Res::Err("DFIP2203 currently disabled for token %s", obj.source.nTokenId.ToString());
            }
        } else {
            DCT_ID id{};
            const auto token = mnview.GetTokenGuessId("DUSD", id);
            if (!token) {
                return Res::Err("No DUSD token defined");
            }

            if (!mnview.GetFixedIntervalPrice({"DFI", "USD"})) {
                return Res::Err("DFI / DUSD fixed interval price not found");
            }

            if (obj.destination != id.v) {
                return Res::Err("Incorrect destination defined for DFI swap, DUSD destination expected id: %d", id.v);
            }
        }
    }

    const auto contractType = dfiToDUSD ? SMART_CONTRACT_DFIP2206F : SMART_CONTRACT_DFIP_2203;
    const auto contractAddressValue = GetFutureSwapContractAddress(contractType);
    if (!contractAddressValue) {
        return contractAddressValue;
    }

    const auto economyKey = dfiToDUSD ? EconomyKeys::DFIP2206FCurrent : EconomyKeys::DFIP2203Current;
    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, economyKey};
    auto balances = attributes->GetValue(liveKey, CBalances{});

    if (height >= static_cast<uint32_t>(consensus.DF16FortCanningCrunchHeight)) {
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

        if (auto res = totalFutures.Sub(obj.source.nValue); !res) {
            return res;
        }

        if (totalFutures.nValue > 0) {
            if (!dfiToDUSD) {
                if (auto res = mnview.StoreFuturesUserValues({height, obj.owner, txn}, {totalFutures, obj.destination});
                    !res) {
                    return res;
                }
            } else {
                if (auto res = mnview.StoreFuturesDUSD({height, obj.owner, txn}, totalFutures.nValue); !res) {
                    return res;
                }
            }
        }

        if (auto res = TransferTokenBalance(obj.source.nTokenId, obj.source.nValue, *contractAddressValue, obj.owner);
            !res) {
            return res;
        }

        if (auto res = balances.Sub(obj.source); !res) {
            return res;
        }
    } else {
        if (height >= static_cast<uint32_t>(consensus.DF23Height) && !dfiToDUSD) {
            CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2211F, DFIPKeys::Active};
            const auto dfip11fEnabled = attributes->GetValue(activeKey, false);
            const auto dusdToken = mnview.GetToken("DUSD");
            if (!dusdToken) {
                return Res::Err("No DUSD token defined");
            }
            const auto dest = !obj.destination ? dusdToken->first.v : obj.destination;
            const auto averageLiquidity = mnview.GetLoanTokenAverageLiquidity({obj.source.nTokenId.v, dest});

            if (dfip11fEnabled && averageLiquidity) {
                CDataStructureV0 averageKey{
                    AttributeTypes::Param, ParamIDs::DFIP2211F, DFIPKeys::AverageLiquidityPercentage};
                const auto averageLiquidityPercentage =
                    attributes->GetValue(averageKey, DEFAULT_AVERAGE_LIQUIDITY_PERCENTAGE);

                const auto maxSwapAmount = MultiplyAmounts(*averageLiquidity, averageLiquidityPercentage);

                arith_uint256 totalSwapAmount{};

                mnview.ForEachFuturesUserValues(
                    [&](const CFuturesUserKey &key, const CFuturesUserValue &futuresValues) {
                        if (futuresValues.source.nTokenId == obj.source.nTokenId &&
                            futuresValues.destination == obj.destination) {
                            totalSwapAmount += futuresValues.source.nValue;
                        }
                        return true;
                    },
                    {height, {}, std::numeric_limits<uint32_t>::max()});

                if (obj.source.nValue + totalSwapAmount > maxSwapAmount) {
                    auto percentageString = GetDecimalString(averageLiquidityPercentage * 100);
                    rtrim(percentageString, '0');
                    if (percentageString.back() == '.') {
                        percentageString.pop_back();
                    }
                    return Res::Err(
                        "Swap amount exceeds %d%% of average pool liquidity limit. Available amount to swap: %s@%s",
                        percentageString,
                        GetDecimalString((maxSwapAmount - totalSwapAmount).GetLow64()),
                        source->symbol);
                }
            }
        }

        if (auto res = TransferTokenBalance(obj.source.nTokenId, obj.source.nValue, obj.owner, *contractAddressValue);
            !res) {
            return res;
        }

        if (!dfiToDUSD) {
            if (auto res = mnview.StoreFuturesUserValues({height, obj.owner, txn}, {obj.source, obj.destination});
                !res) {
                return res;
            }
        } else {
            if (auto res = mnview.StoreFuturesDUSD({height, obj.owner, txn}, obj.source.nValue); !res) {
                return res;
            }
        }
        balances.Add(obj.source);
    }

    attributes->SetValue(liveKey, balances);

    mnview.SetVariable(*attributes);

    return Res::Ok();
}
