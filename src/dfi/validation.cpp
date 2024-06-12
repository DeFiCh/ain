// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <ain_rs_exports.h>
#include <chain.h>
#include <dfi/accountshistory.h>
#include <dfi/errors.h>
#include <dfi/govvariables/attributes.h>
#include <dfi/govvariables/loan_daily_reward.h>
#include <dfi/govvariables/loan_splits.h>
#include <dfi/govvariables/lp_daily_dfi_reward.h>
#include <dfi/govvariables/lp_splits.h>
#include <dfi/historywriter.h>
#include <dfi/masternodes.h>
#include <dfi/mn_checks.h>
#include <dfi/mn_rpc.h>
#include <dfi/threadpool.h>
#include <dfi/validation.h>
#include <dfi/vaulthistory.h>
#include <ffi/ffiexports.h>
#include <ffi/ffihelpers.h>
#include <validation.h>

#include <consensus/params.h>
#include <boost/asio.hpp>

#define MILLI 0.001

using LoanTokenCollection = std::vector<std::pair<DCT_ID, CLoanSetLoanTokenImplementation>>;

struct NullPoolSwapData {
    uint256 txid;
    uint32_t height;
    std::string address;
    CTokenAmount amount;
};

/*
 * Due to a bug in pool swap if a user failed to set a to address, the swap amount
 * was sent to an empty CScript address. The collection below is the list of such
 * transactions. For reference the transaction ID and height is provided along with
 * the address and amount. The address is the source of each pool swap and the amount
 * is the resulting amount of the original swap. These amounts reside on the empty
 * CScript address and will be restored to the original source address.
 *
 * This bug was fixed in the following PR.
 * https://github.com/DeFiCh/ain/pull/1534
 */
static std::vector<NullPoolSwapData> nullPoolSwapAmounts = {
    {uint256S("87606c8d4d4079b2aeeda669b5a17a15c16ddd1eebf11036913a8735b8ecf4ce"),
     582119,  "dX9bZ7XmWSwdArNjswpZLFe12rMcaFK5tC",
     {{2}, 2879}        },
    {uint256S("6726cfcbb6a00d605a5bf83bdcf80b7c3f6d24a7dbfeb4f84d094659380705bf"),
     588961,  "dYBEB3q9sd7e7wi4JKsPdtaWCcrAitQd3K",
     {{0}, 17746031907} },
    {uint256S("fe7f88fa179d5d42845a72ac8058a389f6f32c8f416ae27e807757ced15dfa0e"),
     603251,  "daRtigh64NnuNRvKpECgcpWWJxfXoysL1B",
     {{2}, 15733742}    },
    {uint256S("70933a17bd504198a23d0b76751fe2bc3ea3a59229b8f5bc824a172199a2149b"),
     1393664, "dF9ot6cxhKX8o6BLYYg8jRj29uykjMH4pj",
     {{0}, 38568286}    },
    {uint256S("85c0281c72c2c198e5d315174b8af17d34d0f8649593bb1f0d72820d72033583"),
     1394217, "dFvadXjXApXbzdPDbzHdqRtqi3FRgR4bJF",
     {{15}, 2786945615} },
    {uint256S("b1a46fdb400ebb802da48b92a55ed1a80f55389bc734d6b851a5d27657c2aab3"),
     1514756, "dYGKdwGU5QGMFUz8jhCEe54GjLKkyMoYmw",
     {{15}, 539588954}  },
    {uint256S("393609be8ab41bda8e139673aa63d03fd2d6a9b9d34aa79ffe059ac286acdebb"),
     1546518, "dEN8ASewehaiirxSi2wXh7uthuFyuByjWi",
     {{0}, 21555036213} },
    {uint256S("48589a782be651e76279cb2eaf3196c574cd28ec443d548cca3ac5a769a49915"),
     1634162, "dEzuYZ2ow4nRnzHYUiedj12DzCmKGpcwrX",
     {{0}, 0}           },
    {uint256S("17b7ab18074877dd35ea09925b9a00b17d450d1bcff631e000793f298a945586"),
     1791032, "dSBVE8ovbCMXCzjPRdpyEkGMspk6nfGHdo",
     {{26}, 1264}       },
    {uint256S("2f00758226522a43e7bb99104572f481268fa9d8da66ecd38069f32975ec5852"),
     1791050, "dSBVE8ovbCMXCzjPRdpyEkGMspk6nfGHdo",
     {{26}, 84}         },
    {uint256S("b6410b56257c4cb8e7299c3908db19971c70578e55ce7a297f064474ff2490c2"),
     1791054, "dSBVE8ovbCMXCzjPRdpyEkGMspk6nfGHdo",
     {{26}, 42}         },
    {uint256S("de1d6f9a701b458218dec5d98722b38d939a4cfb958ead0387b032b59cc77e1a"),
     1805337, "dSBVE8ovbCMXCzjPRdpyEkGMspk6nfGHdo",
     {{15}, 1467276}    },
    {uint256S("c511317d0a5da24246333aff63e0a941116fdbd595835bdf1dc31d153bb32075"),
     1805392, "dSBVE8ovbCMXCzjPRdpyEkGMspk6nfGHdo",
     {{15}, 132799941}  },
    {uint256S("f4d3732def5cc2aeab2e11312c9e9e4d98394b85fc9345d4377da49e2ee95496"),
     1805412, "dSBVE8ovbCMXCzjPRdpyEkGMspk6nfGHdo",
     {{0}, 263914}      },
    {uint256S("c18f19d355d6add3b9776f3195026b231fbf3047caa614794d927379939fa62d"),
     1808344, "dSBVE8ovbCMXCzjPRdpyEkGMspk6nfGHdo",
     {{0}, 451934}      },
    {uint256S("0a09b6d132661619f044a00992d5f0e129d79e20fe8b0c2098698c847979fe75"),
     1808505, "dSBVE8ovbCMXCzjPRdpyEkGMspk6nfGHdo",
     {{0}, 29492}       },
    {uint256S("403d2f69e1a7216b4b654c090424b3ac24de0951a2399ec7d12375b889b8636a"),
     1808517, "dSBVE8ovbCMXCzjPRdpyEkGMspk6nfGHdo",
     {{15}, 3679962}    },
    {uint256S("42eb84e03d03200896a19608731989cccf7401be6439fe71f3aba3ba2d2d9aa3"),
     1808525, "dSBVE8ovbCMXCzjPRdpyEkGMspk6nfGHdo",
     {{15}, 43318576}   },
    {uint256S("30d4fc8d0940c8a27f72e7aff584e834e4225c6d65006008fbf4beefe1156d28"),
     1808534, "dSBVE8ovbCMXCzjPRdpyEkGMspk6nfGHdo",
     {{15}, 43318011}   },
    {uint256S("7bd6631a6f836f8fc9cf221924afdd70cbf6882baeafa5a58e5942a2920d368e"),
     1808594, "dSBVE8ovbCMXCzjPRdpyEkGMspk6nfGHdo",
     {{15}, 43316180}   },
    {uint256S("d0c731bc4ed71a832e96342db07207fc6ed72cc9e594f829d470f60f6dcdfb81"),
     1808614, "dSBVE8ovbCMXCzjPRdpyEkGMspk6nfGHdo",
     {{68}, 2}          },
    {uint256S("9767ce6592650ea8f33763c5c3413d19b66d9d981a1260441681ea559053623d"),
     1809421, "dSBVE8ovbCMXCzjPRdpyEkGMspk6nfGHdo",
     {{15}, 15603}      },
    {uint256S("2c95644f7e69029c0187d4fea3e0bada058db166b78bbb9085a33f7819152aa8"),
     1810446, "8FXJVWVwDDjqWvSspCmGQ2s1HayPyUkSi4",
     {{15}, 212403799}  },
    {uint256S("244c366093ea7ef8cd6e8830ccf3490a8d00475271d6aea3c06179791f72dcc1"),
     1812405, "8FXJVWVwDDjqWvSspCmGQ2s1HayPyUkSi4",
     {{0}, 54140884}    },
    {uint256S("f233573b41577a0b12abe82babb41faa5dd602e99798175df09204a59e40ce4c"),
     1919436, "dac8o4Qw9KyWPyuiSmvU4K91BHGkJ6Ne2y",
     {{15}, 191772603}  },
    {uint256S("d7a8807bc3e0aebf5db4b7cd392698a3e3213a2b33738091bf085b24b2d760fb"),
     1919436, "dac8o4Qw9KyWPyuiSmvU4K91BHGkJ6Ne2y",
     {{15}, 159432864}  },
    {uint256S("c2a1523edcc75043e0dc8fdd5d06a0c414658f6140cdfc85430c4dd93120f9df"),
     1919438, "dac8o4Qw9KyWPyuiSmvU4K91BHGkJ6Ne2y",
     {{15}, 97167004}   },
    {uint256S("fd7ee2e7f8b184cb02bea04f8aa0e0bbff038659e241316ae8846e57810a173d"),
     1919438, "dac8o4Qw9KyWPyuiSmvU4K91BHGkJ6Ne2y",
     {{15}, 32948497}   },
    {uint256S("72cb7e4bee5ed9ac59cd26d08f52d9db3147e9735c6360c94cafef0b13109538"),
     1919438, "dac8o4Qw9KyWPyuiSmvU4K91BHGkJ6Ne2y",
     {{15}, 2647089470} },
    {uint256S("9c262cda4088a3c2c5c16eef3d3df0e5917a16d01b83b7b6dddd6df14b7904e3"),
     1919438, "dac8o4Qw9KyWPyuiSmvU4K91BHGkJ6Ne2y",
     {{15}, 136219703}  },
    {uint256S("25f42fa88d8aae0442fb5f001a9408a152b5434cf33becf29f739ede7f179b2e"),
     1919438, "dac8o4Qw9KyWPyuiSmvU4K91BHGkJ6Ne2y",
     {{15}, 1108217183} },
    {uint256S("42bf12b1a847397186fa015a81fb74c13965fcef608b3dacfbc6b8a444717e4c"),
     1919438, "dac8o4Qw9KyWPyuiSmvU4K91BHGkJ6Ne2y",
     {{15}, 22813721}   },
    {uint256S("1a4075603abed93c640a89fcdb720d6bae82562dc7fa85969bf12b4e15da9de2"),
     1919438, "dac8o4Qw9KyWPyuiSmvU4K91BHGkJ6Ne2y",
     {{15}, 19564673}   },
    {uint256S("89fd2b6493283b3a6ffc65353d9d670bc72951b490d865b4d3a293ae749c6c5d"),
     1919438, "dac8o4Qw9KyWPyuiSmvU4K91BHGkJ6Ne2y",
     {{15}, 881176354}  },
    {uint256S("9b593d5d3c08b8357a72e4736b3629e4d0fa5bb6eda21626f97c55ab90f82603"),
     1919438, "dac8o4Qw9KyWPyuiSmvU4K91BHGkJ6Ne2y",
     {{15}, 1266342401} },
    {uint256S("18db206764c5a54df3308593c5df1f7c5b9cefe041b9da06ed637e9f873b33d9"),
     1919438, "dac8o4Qw9KyWPyuiSmvU4K91BHGkJ6Ne2y",
     {{15}, 637813546}  },
    {uint256S("7c2e806a317a573076f83948d5dea3725b5467fbe565fbbe2ac0f895eb50b2da"),
     2237402, "df1q734dll45dug5prgxznuvg7wdq2avsc20dpr3wl",
     {{13}, 49800219866}},
    {uint256S("5fd8479f4a3f831b36eff8d732893755c760d245d9d8f22bf7c16e541246c3cb"),
     2259206, "dJs8vikW87E1M3e5oe4N6hUpBs89Dhh77S",
     {{15}, 1}          },
    {uint256S("86934063307d74a32354cd07cb0969e5ff7eed592e5d8a88b4b5ace0ae55262b"),
     2269592, "dEPoXJzwGia1aAbz6ZRB7FFSKSeWPn1v7A",
     {{2}, 11472400}    },
};

template <typename GovVar>
static void UpdateDailyGovVariables(const std::map<CommunityAccountType, uint32_t>::const_iterator &incentivePair,
                                    CCustomCSView &cache,
                                    int nHeight) {
    if (incentivePair != Params().GetConsensus().blockTokenRewards.end()) {
        CAmount subsidy =
            CalculateCoinbaseReward(GetBlockSubsidy(nHeight, Params().GetConsensus()), incentivePair->second);
        subsidy *= Params().GetConsensus().blocksPerDay();
        // Change daily LP reward if it has changed
        auto var = cache.GetVariable(GovVar::TypeName());
        if (var) {
            // Cast to avoid UniValue in GovVariable Export/ImportserliazedSplits.emplace(it.first.v, it.second);
            auto lpVar = dynamic_cast<GovVar *>(var.get());
            if (lpVar && lpVar->dailyReward != subsidy) {
                lpVar->dailyReward = subsidy;
                lpVar->Apply(cache, nHeight);
                cache.SetVariable(*lpVar);
            }
        }
    }
}

static void ProcessRewardEvents(const CBlockIndex *pindex, CCustomCSView &cache, const Consensus::Params &consensus) {
    // Hard coded LP_DAILY_DFI_REWARD change
    if (pindex->nHeight >= consensus.DF8EunosHeight) {
        const auto &incentivePair = consensus.blockTokenRewards.find(CommunityAccountType::IncentiveFunding);
        UpdateDailyGovVariables<LP_DAILY_DFI_REWARD>(incentivePair, cache, pindex->nHeight);
    }

    // Hard coded LP_DAILY_LOAN_TOKEN_REWARD change
    if (pindex->nHeight >= consensus.DF11FortCanningHeight) {
        const auto &incentivePair = consensus.blockTokenRewards.find(CommunityAccountType::Loan);
        UpdateDailyGovVariables<LP_DAILY_LOAN_TOKEN_REWARD>(incentivePair, cache, pindex->nHeight);
    }

    // hardfork commissions update
    const auto distributed = cache.UpdatePoolRewards(
        [&](const CScript &owner, DCT_ID tokenID) {
            cache.CalculateOwnerRewards(owner, pindex->nHeight);
            return cache.GetBalance(owner, tokenID);
        },
        [&](const CScript &from, const CScript &to, CTokenAmount amount) {
            if (!from.empty()) {
                auto res = cache.SubBalance(from, amount);
                if (!res) {
                    LogPrintf("Custom pool rewards: can't subtract balance of %s: %s, height %ld\n",
                              from.GetHex(),
                              res.msg,
                              pindex->nHeight);
                    return res;
                }
            }
            if (!to.empty()) {
                auto res = cache.AddBalance(to, amount);
                if (!res) {
                    LogPrintf("Can't apply reward to %s: %s, %ld\n", to.GetHex(), res.msg, pindex->nHeight);
                    return res;
                }
                cache.UpdateBalancesHeight(to, pindex->nHeight + 1);
            }
            return Res::Ok();
        },
        pindex->nHeight);

    auto res = cache.SubCommunityBalance(CommunityAccountType::IncentiveFunding, distributed.first);
    if (!res.ok) {
        LogPrintf("Pool rewards: can't update community balance: %s. Block %ld (%s)\n",
                  res.msg,
                  pindex->nHeight,
                  pindex->phashBlock->GetHex());
    } else {
        if (distributed.first != 0) {
            LogPrint(BCLog::ACCOUNTCHANGE,
                     "AccountChange: event=ProcessRewardEvents fund=%s change=%s\n",
                     GetCommunityAccountName(CommunityAccountType::IncentiveFunding),
                     (CBalances{{{{0}, -distributed.first}}}.ToString()));
        }
    }

    if (pindex->nHeight >= consensus.DF11FortCanningHeight) {
        res = cache.SubCommunityBalance(CommunityAccountType::Loan, distributed.second);
        if (!res.ok) {
            LogPrintf("Pool rewards: can't update community balance: %s. Block %ld (%s)\n",
                      res.msg,
                      pindex->nHeight,
                      pindex->phashBlock->GetHex());
        } else {
            if (distributed.second != 0) {
                LogPrint(BCLog::ACCOUNTCHANGE,
                         "AccountChange: event=ProcessRewardEvents fund=%s change=%s\n",
                         GetCommunityAccountName(CommunityAccountType::Loan),
                         (CBalances{{{{0}, -distributed.second}}}.ToString()));
            }
        }
    }
}

static void ProcessICXEvents(const CBlockIndex *pindex, CCustomCSView &cache, const Consensus::Params &consensus) {
    if (pindex->nHeight < consensus.DF8EunosHeight) {
        return;
    }

    bool isPreEunosPaya = pindex->nHeight < consensus.DF10EunosPayaHeight;

    cache.ForEachICXOrderExpire(
        [&](const CICXOrderView::StatusKey &key, uint8_t status) {
            if (static_cast<int>(key.first) != pindex->nHeight) {
                return false;
            }

            auto order = cache.GetICXOrderByCreationTx(key.second);
            if (!order) {
                return true;
            }

            if (order->orderType == CICXOrder::TYPE_INTERNAL) {
                CTokenAmount amount{order->idToken, order->amountToFill};
                CScript txidaddr(order->creationTx.begin(), order->creationTx.end());
                auto res = cache.SubBalance(txidaddr, amount);
                if (!res) {
                    LogPrintf(
                        "Can't subtract balance from order (%s) txidaddr: %s\n", order->creationTx.GetHex(), res.msg);
                } else {
                    cache.CalculateOwnerRewards(order->ownerAddress, pindex->nHeight);
                    cache.AddBalance(order->ownerAddress, amount);
                }
            }

            cache.ICXCloseOrderTx(*order, status);

            return true;
        },
        pindex->nHeight);

    cache.ForEachICXMakeOfferExpire(
        [&](const CICXOrderView::StatusKey &key, uint8_t status) {
            if (static_cast<int>(key.first) != pindex->nHeight) {
                return false;
            }

            auto offer = cache.GetICXMakeOfferByCreationTx(key.second);
            if (!offer) {
                return true;
            }

            auto order = cache.GetICXOrderByCreationTx(offer->orderTx);
            if (!order) {
                return true;
            }

            CScript txidAddr(offer->creationTx.begin(), offer->creationTx.end());
            CTokenAmount takerFee{DCT_ID{0}, offer->takerFee};

            if ((order->orderType == CICXOrder::TYPE_INTERNAL &&
                 !cache.ExistedICXSubmitDFCHTLC(offer->creationTx, isPreEunosPaya)) ||
                (order->orderType == CICXOrder::TYPE_EXTERNAL &&
                 !cache.ExistedICXSubmitEXTHTLC(offer->creationTx, isPreEunosPaya))) {
                auto res = cache.SubBalance(txidAddr, takerFee);
                if (!res) {
                    LogPrintf(
                        "Can't subtract takerFee from offer (%s) txidAddr: %s\n", offer->creationTx.GetHex(), res.msg);
                } else {
                    cache.CalculateOwnerRewards(offer->ownerAddress, pindex->nHeight);
                    cache.AddBalance(offer->ownerAddress, takerFee);
                }
            }

            cache.ICXCloseMakeOfferTx(*offer, status);

            return true;
        },
        pindex->nHeight);

    cache.ForEachICXSubmitDFCHTLCExpire(
        [&](const CICXOrderView::StatusKey &key, uint8_t status) {
            if (static_cast<int>(key.first) != pindex->nHeight) {
                return false;
            }

            auto dfchtlc = cache.GetICXSubmitDFCHTLCByCreationTx(key.second);
            if (!dfchtlc) {
                return true;
            }

            auto offer = cache.GetICXMakeOfferByCreationTx(dfchtlc->offerTx);
            if (!offer) {
                return true;
            }

            auto order = cache.GetICXOrderByCreationTx(offer->orderTx);
            if (!order) {
                return true;
            }

            bool refund = false;

            if (status == CICXSubmitDFCHTLC::STATUS_EXPIRED && order->orderType == CICXOrder::TYPE_INTERNAL) {
                if (!cache.ExistedICXSubmitEXTHTLC(dfchtlc->offerTx, isPreEunosPaya)) {
                    CTokenAmount makerDeposit{DCT_ID{0}, offer->takerFee};
                    cache.CalculateOwnerRewards(order->ownerAddress, pindex->nHeight);
                    cache.AddBalance(order->ownerAddress, makerDeposit);
                    refund = true;
                }
            } else if (status == CICXSubmitDFCHTLC::STATUS_REFUNDED) {
                refund = true;
            }

            if (refund) {
                CScript ownerAddress;
                if (order->orderType == CICXOrder::TYPE_INTERNAL) {
                    ownerAddress = CScript(order->creationTx.begin(), order->creationTx.end());
                } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
                    ownerAddress = offer->ownerAddress;
                }

                CTokenAmount amount{order->idToken, dfchtlc->amount};
                CScript txidaddr = CScript(dfchtlc->creationTx.begin(), dfchtlc->creationTx.end());
                auto res = cache.SubBalance(txidaddr, amount);
                if (!res) {
                    LogPrintf("Can't subtract balance from dfc htlc (%s) txidaddr: %s\n",
                              dfchtlc->creationTx.GetHex(),
                              res.msg);
                } else {
                    cache.CalculateOwnerRewards(ownerAddress, pindex->nHeight);
                    cache.AddBalance(ownerAddress, amount);
                }

                cache.ICXCloseDFCHTLC(*dfchtlc, status);
            }

            return true;
        },
        pindex->nHeight);

    cache.ForEachICXSubmitEXTHTLCExpire(
        [&](const CICXOrderView::StatusKey &key, uint8_t status) {
            if (static_cast<int>(key.first) != pindex->nHeight) {
                return false;
            }

            auto exthtlc = cache.GetICXSubmitEXTHTLCByCreationTx(key.second);
            if (!exthtlc) {
                return true;
            }

            auto offer = cache.GetICXMakeOfferByCreationTx(exthtlc->offerTx);
            if (!offer) {
                return true;
            }

            auto order = cache.GetICXOrderByCreationTx(offer->orderTx);
            if (!order) {
                return true;
            }

            if (status == CICXSubmitEXTHTLC::STATUS_EXPIRED && order->orderType == CICXOrder::TYPE_EXTERNAL) {
                if (!cache.ExistedICXSubmitDFCHTLC(exthtlc->offerTx, isPreEunosPaya)) {
                    CTokenAmount makerDeposit{DCT_ID{0}, offer->takerFee};
                    cache.CalculateOwnerRewards(order->ownerAddress, pindex->nHeight);
                    cache.AddBalance(order->ownerAddress, makerDeposit);
                    cache.ICXCloseEXTHTLC(*exthtlc, status);
                }
            }

            return true;
        },
        pindex->nHeight);
}

static uint32_t GetNextBurnPosition() {
    return nPhantomBurnTx++;
}

// Burn non-transaction amounts, that is burns that are not sent directly to the burn address
// in a account or UTXO transaction. When parsing TXs via ConnectBlock that result in a burn
// from an account in this way call the function below. This will add the burn to the map to
// be added to the burn index as a phantom TX appended to the end of the connecting block.
Res AddNonTxToBurnIndex(const CScript &from, const CBalances &amounts) {
    return mapBurnAmounts[from].AddBalances(amounts.balances);
}

static void ProcessEunosEvents(const CBlockIndex *pindex, CCustomCSView &cache, const Consensus::Params &consensus) {
    if (pindex->nHeight != consensus.DF8EunosHeight) {
        return;
    }

    // Move funds from old burn address to new one
    CBalances burnAmounts;
    cache.ForEachBalance(
        [&burnAmounts](const CScript &owner, CTokenAmount balance) {
            if (owner != Params().GetConsensus().retiredBurnAddress) {
                return false;
            }

            burnAmounts.Add({balance.nTokenId, balance.nValue});

            return true;
        },
        BalanceKey{consensus.retiredBurnAddress, DCT_ID{}});

    AddNonTxToBurnIndex(consensus.retiredBurnAddress, burnAmounts);

    // Zero foundation balances
    for (const auto &script : consensus.accountDestruction) {
        CBalances zeroAmounts;
        cache.ForEachBalance(
            [&zeroAmounts, script](const CScript &owner, CTokenAmount balance) {
                if (owner != script) {
                    return false;
                }

                zeroAmounts.Add({balance.nTokenId, balance.nValue});

                return true;
            },
            BalanceKey{script, DCT_ID{}});

        cache.SubBalances(script, zeroAmounts);
    }

    // Add any non-Tx burns to index as phantom Txs
    for (const auto &item : mapBurnAmounts) {
        for (const auto &subItem : item.second.balances) {
            // If amount cannot be deducted then burn skipped.
            auto result = cache.SubBalance(item.first, {subItem.first, subItem.second});
            if (result.ok) {
                cache.AddBalance(consensus.burnAddress, {subItem.first, subItem.second});

                // Add transfer as additional TX in block
                cache.GetHistoryWriters().WriteAccountHistory({Params().GetConsensus().burnAddress,
                                                               static_cast<uint32_t>(pindex->nHeight),
                                                               GetNextBurnPosition()},
                                                              {uint256{},
                                                               static_cast<uint8_t>(CustomTxType::AccountToAccount),
                                                               {{subItem.first, subItem.second}}});
            } else  // Log burn failure
            {
                CTxDestination dest;
                ExtractDestination(item.first, dest);
                LogPrintf("Burn failed: %s Address: %s Token: %d Amount: %d\n",
                          result.msg,
                          EncodeDestination(dest),
                          subItem.first.v,
                          subItem.second);
            }
        }
    }

    mapBurnAmounts.clear();
}

static void ProcessOracleEvents(const CBlockIndex *pindex, CCustomCSView &cache, const Consensus::Params &consensus) {
    if (pindex->nHeight < consensus.DF11FortCanningHeight) {
        return;
    }
    auto blockInterval = cache.GetIntervalBlock();
    if (pindex->nHeight % blockInterval != 0) {
        return;
    }
    cache.ForEachFixedIntervalPrice([&](const CTokenCurrencyPair &, CFixedIntervalPrice fixedIntervalPrice) {
        // Ensure that we update active and next regardless of state of things
        // And SetFixedIntervalPrice on each evaluation of this block.

        // As long as nextPrice exists, move the buffers.
        // If nextPrice doesn't exist, active price is retained.
        // nextPrice starts off as empty. Will be replaced by the next
        // aggregate, as long as there's a new price available.
        // If there is no price, nextPrice will remain empty.
        // This guarantees that the last price will continue to exists,
        // while the overall validity check still fails.

        // Furthermore, the time stamp is always indicative of the
        // last price time.
        auto nextPrice = fixedIntervalPrice.priceRecord[1];
        if (nextPrice > 0) {
            fixedIntervalPrice.priceRecord[0] = fixedIntervalPrice.priceRecord[1];
        }
        // keep timestamp updated
        fixedIntervalPrice.timestamp = pindex->nTime;
        // Use -1 to indicate empty price
        fixedIntervalPrice.priceRecord[1] = -1;
        auto aggregatePrice = GetAggregatePrice(
            cache, fixedIntervalPrice.priceFeedId.first, fixedIntervalPrice.priceFeedId.second, pindex->nTime);
        if (aggregatePrice) {
            fixedIntervalPrice.priceRecord[1] = aggregatePrice;
        } else {
            LogPrint(BCLog::ORACLE, "ProcessOracleEvents(): No aggregate price available: %s\n", aggregatePrice.msg);
        }
        auto res = cache.SetFixedIntervalPrice(fixedIntervalPrice);
        if (!res) {
            LogPrintf("Error: SetFixedIntervalPrice failed: %s\n", res.msg);
        }
        return true;
    });
}

std::vector<CAuctionBatch> CollectAuctionBatches(const CVaultAssets &vaultAssets,
                                                 const TAmounts &collBalances,
                                                 const TAmounts &loanBalances) {
    constexpr const uint64_t batchThreshold = 10000 * COIN;  // 10k USD
    auto totalCollateralsValue = vaultAssets.totalCollaterals;
    auto totalLoansValue = vaultAssets.totalLoans;

    auto maxCollateralsValue = totalCollateralsValue;
    auto maxLoansValue = totalLoansValue;
    auto maxCollBalances = collBalances;

    auto CreateAuctionBatch = [&maxCollBalances, &collBalances](CTokenAmount loanAmount, CAmount chunk) {
        CAuctionBatch batch{};
        batch.loanAmount = loanAmount;
        for (const auto &tAmount : collBalances) {
            auto &maxCollBalance = maxCollBalances[tAmount.first];
            auto collValue = std::min(MultiplyAmounts(tAmount.second, chunk), maxCollBalance);
            batch.collaterals.Add({tAmount.first, collValue});
            maxCollBalance -= collValue;
        }
        return batch;
    };

    std::vector<CAuctionBatch> batches;
    for (const auto &loan : vaultAssets.loans) {
        auto maxLoanAmount = loanBalances.at(loan.nTokenId);
        auto loanChunk = std::min(uint64_t(DivideAmounts(loan.nValue, totalLoansValue)), maxLoansValue);
        auto collateralChunkValue =
            std::min(uint64_t(MultiplyAmounts(loanChunk, totalCollateralsValue)), maxCollateralsValue);
        if (collateralChunkValue > batchThreshold) {
            auto chunk = DivideAmounts(batchThreshold, collateralChunkValue);
            auto loanAmount = MultiplyAmounts(maxLoanAmount, chunk);
            for (auto chunks = COIN; chunks > 0; chunks -= chunk) {
                chunk = std::min(static_cast<CAmount>(chunk), chunks);
                loanAmount = std::min(loanAmount, maxLoanAmount);
                auto collateralChunk = MultiplyAmounts(chunk, loanChunk);
                batches.push_back(CreateAuctionBatch({loan.nTokenId, loanAmount}, collateralChunk));
                maxLoanAmount -= loanAmount;
            }
        } else {
            auto loanAmount = CTokenAmount{loan.nTokenId, maxLoanAmount};
            batches.push_back(CreateAuctionBatch(loanAmount, loanChunk));
        }
        maxLoansValue -= loan.nValue;
        maxCollateralsValue -= collateralChunkValue;
    }
    // return precision loss balanced
    for (auto &collateral : maxCollBalances) {
        auto it = batches.begin();
        auto lastValue = collateral.second;
        while (collateral.second > 0) {
            if (it == batches.end()) {
                it = batches.begin();
                if (lastValue == collateral.second) {
                    // we fail to update any batch
                    // extreme small collateral going to first batch
                    it->collaterals.Add({collateral.first, collateral.second});
                    break;
                }
                lastValue = collateral.second;
            }
            if (it->collaterals.balances.count(collateral.first) > 0) {
                it->collaterals.Add({collateral.first, 1});
                --collateral.second;
            }
            ++it;
        }
    }
    return batches;
}

static void ProcessLoanEvents(const CBlockIndex *pindex, CCustomCSView &cache, const Consensus::Params &consensus) {
    if (pindex->nHeight < consensus.DF11FortCanningHeight) {
        return;
    }

    std::vector<CLoanSchemeMessage> loanUpdates;
    cache.ForEachDelayedLoanScheme(
        [&pindex, &loanUpdates](const std::pair<std::string, uint64_t> &key, const CLoanSchemeMessage &loanScheme) {
            if (key.second == static_cast<uint64_t>(pindex->nHeight)) {
                loanUpdates.push_back(loanScheme);
            }
            return true;
        });

    for (const auto &loanScheme : loanUpdates) {
        // Make sure loan still exist, that it has not been destroyed in the mean time.
        if (cache.GetLoanScheme(loanScheme.identifier)) {
            cache.StoreLoanScheme(loanScheme);
        }
        cache.EraseDelayedLoanScheme(loanScheme.identifier, pindex->nHeight);
    }

    std::vector<std::string> loanDestruction;
    cache.ForEachDelayedDestroyScheme([&pindex, &loanDestruction](const std::string &key, const uint64_t &height) {
        if (height == static_cast<uint64_t>(pindex->nHeight)) {
            loanDestruction.push_back(key);
        }
        return true;
    });

    for (const auto &loanDestroy : loanDestruction) {
        cache.EraseLoanScheme(loanDestroy);
        cache.EraseDelayedDestroyScheme(loanDestroy);
    }

    if (!loanDestruction.empty()) {
        CCustomCSView viewCache(cache);
        auto defaultLoanScheme = cache.GetDefaultLoanScheme();
        cache.ForEachVault([&](const CVaultId &vaultId, CVaultData vault) {
            if (!cache.GetLoanScheme(vault.schemeId)) {
                vault.schemeId = *defaultLoanScheme;
                viewCache.UpdateVault(vaultId, vault);
            }
            return true;
        });
        viewCache.Flush();
    }

    if (pindex->nHeight % consensus.blocksCollateralizationRatioCalculation() == 0) {
        bool useNextPrice = false, requireLivePrice = true;

        auto &pool = DfTxTaskPool->pool;

        struct VaultWithCollateralInfo {
            CVaultId vaultId;
            CBalances collaterals;
            CVaultAssets vaultAssets;
            CVaultData vault;
        };

        struct LiquidationVaults {
        public:
            AtomicMutex m;
            std::vector<VaultWithCollateralInfo> vaults;
        };
        LiquidationVaults lv;

        TaskGroup g;

        const auto markCompleted = [&g] { g.RemoveTask(); };

        cache.ForEachVaultCollateral([&](const CVaultId &vaultId, const CBalances &collaterals) {
            g.AddTask();

            CVaultId vaultIdCopy = vaultId;
            CBalances collateralsCopy = collaterals;

            boost::asio::post(
                pool,
                [vaultIdCopy, collateralsCopy, &cache, pindex, useNextPrice, requireLivePrice, &lv, &markCompleted] {
                    auto vaultId = vaultIdCopy;
                    auto collaterals = collateralsCopy;

                    auto vaultAssets = cache.GetVaultAssets(
                        vaultId, collaterals, pindex->nHeight, pindex->nTime, useNextPrice, requireLivePrice);

                    if (!vaultAssets) {
                        markCompleted();
                        return;
                    }

                    auto vault = cache.GetVault(vaultId);
                    assert(vault);

                    auto scheme = cache.GetLoanScheme(vault->schemeId);
                    assert(scheme);

                    if (scheme->ratio <= vaultAssets.val->ratio()) {
                        // All good, within ratio, nothing more to do.
                        markCompleted();
                        return;
                    }

                    {
                        std::unique_lock lock{lv.m};
                        lv.vaults.push_back(VaultWithCollateralInfo{vaultId, collaterals, vaultAssets, *vault});
                    }
                    markCompleted();
                });
            return true;
        });

        g.WaitForCompletion();

        {
            std::unique_lock lock{lv.m};
            for (auto &[vaultId, collaterals, vaultAssets, vault] : lv.vaults) {
                // Time to liquidate vault.
                vault.isUnderLiquidation = true;
                cache.StoreVault(vaultId, vault);
                auto loanTokens = cache.GetLoanTokens(vaultId);
                assert(loanTokens);

                // Get the interest rate for each loan token in the vault, find
                // the interest value and move it to the totals, removing it from the
                // vault, while also stopping the vault from accumulating interest
                // further. Note, however, it's added back so that it's accurate
                // for auction calculations.
                CBalances totalInterest;
                for (auto it = loanTokens->balances.begin(); it != loanTokens->balances.end();) {
                    const auto &[tokenId, tokenValue] = *it;

                    auto rate = cache.GetInterestRate(vaultId, tokenId, pindex->nHeight);
                    assert(rate);

                    auto subInterest = TotalInterest(*rate, pindex->nHeight);
                    if (subInterest > 0) {
                        totalInterest.Add({tokenId, subInterest});
                    }

                    // Remove loan from the vault
                    cache.SubLoanToken(vaultId, {tokenId, tokenValue});

                    if (const auto token = cache.GetToken("DUSD"); token && token->first == tokenId) {
                        TrackDUSDSub(cache, {tokenId, tokenValue});
                    }

                    // Remove interest from the vault
                    cache.DecreaseInterest(pindex->nHeight,
                                           vaultId,
                                           vault.schemeId,
                                           tokenId,
                                           tokenValue,
                                           subInterest < 0 || (!subInterest && rate->interestPerBlock.negative)
                                               ? std::numeric_limits<CAmount>::max()
                                               : subInterest);

                    // Putting this back in now for auction calculations.
                    it->second += subInterest;

                    // If loan amount fully negated then remove it
                    if (it->second < 0) {
                        TrackNegativeInterest(cache, {tokenId, tokenValue});

                        it = loanTokens->balances.erase(it);
                    } else {
                        if (subInterest < 0) {
                            TrackNegativeInterest(cache, {tokenId, std::abs(subInterest)});
                        }

                        ++it;
                    }
                }

                // Remove the collaterals out of the vault.
                // (Prep to get the auction batches instead)
                for (const auto &col : collaterals.balances) {
                    auto tokenId = col.first;
                    auto tokenValue = col.second;
                    cache.SubVaultCollateral(vaultId, {tokenId, tokenValue});
                }

                auto batches = CollectAuctionBatches(vaultAssets, collaterals.balances, loanTokens->balances);

                // Now, let's add the remaining amounts and store the batch.
                CBalances totalLoanInBatches{};
                for (auto i = 0u; i < batches.size(); i++) {
                    auto &batch = batches[i];
                    totalLoanInBatches.Add(batch.loanAmount);
                    auto tokenId = batch.loanAmount.nTokenId;
                    auto interest = totalInterest.balances[tokenId];
                    if (interest > 0) {
                        auto balance = loanTokens->balances[tokenId];
                        auto interestPart = DivideAmounts(batch.loanAmount.nValue, balance);
                        batch.loanInterest = MultiplyAmounts(interestPart, interest);
                        totalLoanInBatches.Sub({tokenId, batch.loanInterest});
                    }
                    cache.StoreAuctionBatch({vaultId, i}, batch);
                }

                // Check if more than loan amount was generated.
                CBalances balances;
                for (const auto &[tokenId, amount] : loanTokens->balances) {
                    if (totalLoanInBatches.balances.count(tokenId)) {
                        const auto interest =
                            totalInterest.balances.count(tokenId) ? totalInterest.balances[tokenId] : 0;
                        if (totalLoanInBatches.balances[tokenId] > amount - interest) {
                            balances.Add({tokenId, totalLoanInBatches.balances[tokenId] - (amount - interest)});
                        }
                    }
                }

                // Only store to attributes if there has been a rounding error.
                if (!balances.balances.empty()) {
                    TrackLiveBalances(cache, balances, EconomyKeys::BatchRoundingExcess);
                }

                // All done. Ready to save the overall auction.
                cache.StoreAuction(vaultId,
                                   CAuctionData{uint32_t(batches.size()),
                                                pindex->nHeight + consensus.blocksCollateralAuction(),
                                                cache.GetLoanLiquidationPenalty()});

                // Store state in vault DB
                if (pvaultHistoryDB) {
                    pvaultHistoryDB->WriteVaultState(cache, *pindex, vaultId, vaultAssets.ratio());
                }
            }
        }
    }

    CAccountsHistoryWriter view(cache, pindex->nHeight, ~0u, pindex->GetBlockHash(), uint8_t(CustomTxType::AuctionBid));

    view.ForEachVaultAuction(
        [&](const CVaultId &vaultId, const CAuctionData &data) {
            if (data.liquidationHeight != uint32_t(pindex->nHeight)) {
                return false;
            }
            auto vault = view.GetVault(vaultId);
            assert(vault);

            CBalances balances;
            for (uint32_t i = 0; i < data.batchCount; i++) {
                auto batch = view.GetAuctionBatch({vaultId, i});
                assert(batch);

                if (auto bid = view.GetAuctionBid({vaultId, i})) {
                    auto bidOwner = bid->first;
                    auto bidTokenAmount = bid->second;

                    auto penaltyAmount = MultiplyAmounts(batch->loanAmount.nValue, COIN + data.liquidationPenalty);
                    if (bidTokenAmount.nValue < penaltyAmount) {
                        LogPrintf("WARNING: bidTokenAmount.nValue(%d) < penaltyAmount(%d)\n",
                                  bidTokenAmount.nValue,
                                  penaltyAmount);
                    }
                    // penaltyAmount includes interest, batch as well, so we should put interest back
                    // in result we have 5% penalty + interest via DEX to DFI and burn
                    auto amountToBurn = penaltyAmount - batch->loanAmount.nValue + batch->loanInterest;
                    if (amountToBurn > 0) {
                        CScript tmpAddress(vaultId.begin(), vaultId.end());
                        view.AddBalance(tmpAddress, {bidTokenAmount.nTokenId, amountToBurn});
                        SwapToDFIorDUSD(view,
                                        bidTokenAmount.nTokenId,
                                        amountToBurn,
                                        tmpAddress,
                                        consensus.burnAddress,
                                        pindex->nHeight,
                                        consensus);
                    }

                    view.CalculateOwnerRewards(bidOwner, pindex->nHeight);

                    for (const auto &col : batch->collaterals.balances) {
                        auto tokenId = col.first;
                        auto tokenAmount = col.second;
                        view.AddBalance(bidOwner, {tokenId, tokenAmount});
                    }

                    auto amountToFill = bidTokenAmount.nValue - penaltyAmount;
                    if (amountToFill > 0) {
                        // return the rest as collateral to vault via DEX to DFI
                        CScript tmpAddress(vaultId.begin(), vaultId.end());
                        view.AddBalance(tmpAddress, {bidTokenAmount.nTokenId, amountToFill});

                        SwapToDFIorDUSD(view,
                                        bidTokenAmount.nTokenId,
                                        amountToFill,
                                        tmpAddress,
                                        tmpAddress,
                                        pindex->nHeight,
                                        consensus);
                        auto amount = view.GetBalance(tmpAddress, DCT_ID{0});
                        view.SubBalance(tmpAddress, amount);
                        view.AddVaultCollateral(vaultId, amount);
                    }

                    auto res = view.SubMintedTokens(batch->loanAmount.nTokenId,
                                                    batch->loanAmount.nValue - batch->loanInterest);
                    if (!res) {
                        LogPrintf("AuctionBid: SubMintedTokens failed: %s\n", res.msg);
                    }

                    AuctionHistoryKey key{data.liquidationHeight, bidOwner, vaultId, i};
                    AuctionHistoryValue value{bidTokenAmount, batch->collaterals.balances};
                    cache.GetHistoryWriters().WriteAuctionHistory(key, value);

                } else {
                    // we should return loan including interest
                    view.AddLoanToken(vaultId, batch->loanAmount);
                    balances.Add({batch->loanAmount.nTokenId, batch->loanInterest});

                    // When tracking loan amounts remove interest.
                    if (const auto token = view.GetToken("DUSD"); token && token->first == batch->loanAmount.nTokenId) {
                        TrackDUSDAdd(view,
                                     {batch->loanAmount.nTokenId, batch->loanAmount.nValue - batch->loanInterest});
                    }

                    if (auto token = view.GetLoanTokenByID(batch->loanAmount.nTokenId)) {
                        view.IncreaseInterest(pindex->nHeight,
                                              vaultId,
                                              vault->schemeId,
                                              batch->loanAmount.nTokenId,
                                              token->interest,
                                              batch->loanAmount.nValue);
                    }
                    for (const auto &col : batch->collaterals.balances) {
                        auto tokenId = col.first;
                        auto tokenAmount = col.second;
                        view.AddVaultCollateral(vaultId, {tokenId, tokenAmount});
                    }
                }
            }

            // Only store to attributes if there has been a rounding error.
            if (!balances.balances.empty()) {
                TrackLiveBalances(view, balances, EconomyKeys::ConsolidatedInterest);
            }

            vault->isUnderLiquidation = false;
            view.StoreVault(vaultId, *vault);
            view.EraseAuction(vaultId, pindex->nHeight);

            // Store state in vault DB
            cache.GetHistoryWriters().WriteVaultState(view, *pindex, vaultId);

            return true;
        },
        pindex->nHeight);

    view.Flush();
}

static void LiquidityForFuturesLimit(const CBlockIndex *pindex,
                                     CCustomCSView &cache,
                                     const Consensus::Params &consensus,
                                     const LoanTokenCollection &loanTokens,
                                     const bool futureSwapBlock) {
    // Skip on testnet until later height to avoid a TX that hit the limit and was not rejected
    // due to a bug in the initital FutureSwap implementation.
    if ((pindex->nHeight < consensus.DF23Height) ||
        (Params().NetworkIDString() == CBaseChainParams::TESTNET && pindex->nHeight < 1520000)) {
        return;
    }

    auto attributes = cache.GetAttributes();

    CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2211F, DFIPKeys::Active};
    if (!attributes->GetValue(activeKey, false)) {
        return;
    }

    CDataStructureV0 samplingKey{AttributeTypes::Param, ParamIDs::DFIP2211F, DFIPKeys::LiquidityCalcSamplingPeriod};
    const auto samplingPeriod = attributes->GetValue(samplingKey, DEFAULT_LIQUIDITY_CALC_SAMPLING_PERIOD);
    if ((pindex->nHeight - consensus.DF23Height) % samplingPeriod != 0) {
        return;
    }

    CDataStructureV0 blockKey{AttributeTypes::Param, ParamIDs::DFIP2211F, DFIPKeys::BlockPeriod};
    const auto blockPeriod = attributes->GetValue(blockKey, DEFAULT_FS_LIQUIDITY_BLOCK_PERIOD);

    const auto dusdToken = cache.GetToken("DUSD");
    if (!dusdToken) {
        return;
    }

    const auto &dusdID = dusdToken->first;

    std::set<DCT_ID> tokens;
    for (const auto &[id, loanToken] : loanTokens) {
        tokens.insert(id);
    }

    // Filter out DUSD
    tokens.erase(dusdID);

    // Store liquidity for loan tokens
    cache.ForEachPoolPair([&](const DCT_ID &, const CPoolPair &poolPair) {
        // Check for loan token
        const auto tokenA = tokens.count(poolPair.idTokenA);
        const auto tokenB = tokens.count(poolPair.idTokenB);
        if (!tokenA && !tokenB) {
            return true;
        }

        // Make sure this is the DUSD loan token pair
        const auto dusdA = poolPair.idTokenA == dusdID;
        const auto dusdB = poolPair.idTokenB == dusdID;
        if (!dusdA && !dusdB) {
            return true;
        }

        cache.SetLoanTokenLiquidityPerBlock(
            {static_cast<uint32_t>(pindex->nHeight), poolPair.idTokenA.v, poolPair.idTokenB.v}, poolPair.reserveA);
        cache.SetLoanTokenLiquidityPerBlock(
            {static_cast<uint32_t>(pindex->nHeight), poolPair.idTokenB.v, poolPair.idTokenA.v}, poolPair.reserveB);

        return true;
    });

    // Collect old entries to delete
    std::vector<LoanTokenLiquidityPerBlockKey> keysToDelete;
    cache.ForEachTokenLiquidityPerBlock(
        [&](const LoanTokenLiquidityPerBlockKey &key, const CAmount &liquidityPerBlock) {
            if (key.height <= pindex->nHeight - blockPeriod) {
                keysToDelete.push_back(key);
                return true;
            }
            return false;
        });

    // Delete old entries
    for (const auto &key : keysToDelete) {
        cache.EraseTokenLiquidityPerBlock(key);
    }

    if (!futureSwapBlock) {
        return;
    }

    // Get liquidity per block for each token
    std::map<LoanTokenAverageLiquidityKey, std::vector<CAmount>> liquidityPerBlockByToken;
    cache.ForEachTokenLiquidityPerBlock(
        [&](const LoanTokenLiquidityPerBlockKey &key, const CAmount &liquidityPerBlock) {
            liquidityPerBlockByToken[{key.sourceID, key.destID}].push_back(liquidityPerBlock);
            return true;
        });

    // Calculate average liquidity for each token
    const auto expectedEntries = blockPeriod / samplingPeriod;
    for (const auto &[key, liquidityPerBlock] : liquidityPerBlockByToken) {
        if (liquidityPerBlock.size() < expectedEntries) {
            cache.EraseTokenAverageLiquidity(key);
            continue;
        }

        arith_uint256 tokenTotal{};
        for (const auto &liquidity : liquidityPerBlock) {
            tokenTotal += liquidity;
        }

        const auto tokenAverage = tokenTotal / expectedEntries;
        LogPrint(BCLog::FUTURESWAP,
                 "Liquidity for future swap limit: token src id: %i, token dest id: %i, new average liquidity: %i\n",
                 key.sourceID,
                 key.destID,
                 tokenAverage.GetLow64());
        cache.SetLoanTokenAverageLiquidity(key, tokenAverage.GetLow64());
    }
}

static auto GetLoanTokensForFutures(CCustomCSView &cache, ATTRIBUTES attributes) {
    LoanTokenCollection loanTokens;

    CDataStructureV0 tokenKey{AttributeTypes::Token, 0, TokenKeys::DFIP2203Enabled};
    cache.ForEachLoanToken([&](const DCT_ID &id, const CLoanView::CLoanSetLoanTokenImpl &loanToken) {
        tokenKey.typeId = id.v;
        const auto enabled = attributes.GetValue(tokenKey, true);
        if (!enabled) {
            return true;
        }

        loanTokens.emplace_back(id, loanToken);

        return true;
    });

    if (loanTokens.empty()) {
        attributes.ForEach(
            [&](const CDataStructureV0 &attr, const CAttributeValue &) {
                if (attr.type != AttributeTypes::Token) {
                    return false;
                }

                tokenKey.typeId = attr.typeId;
                const auto enabled = attributes.GetValue(tokenKey, true);
                if (!enabled) {
                    return true;
                }

                if (attr.key == TokenKeys::LoanMintingEnabled) {
                    auto tokenId = DCT_ID{attr.typeId};
                    if (auto loanToken = cache.GetLoanTokenFromAttributes(tokenId)) {
                        loanTokens.emplace_back(tokenId, *loanToken);
                    }
                }

                return true;
            },
            CDataStructureV0{AttributeTypes::Token});
    }

    return loanTokens;
}

static void ProcessFutures(const CBlockIndex *pindex, CCustomCSView &cache, const Consensus::Params &consensus) {
    if (pindex->nHeight < consensus.DF15FortCanningRoadHeight) {
        return;
    }

    auto attributes = cache.GetAttributes();

    CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::Active};
    CDataStructureV0 blockKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::BlockPeriod};
    CDataStructureV0 rewardKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::RewardPct};
    if (!attributes->GetValue(activeKey, false) || !attributes->CheckKey(blockKey) ||
        !attributes->CheckKey(rewardKey)) {
        return;
    }

    CDataStructureV0 startKey{AttributeTypes::Param, ParamIDs::DFIP2203, DFIPKeys::StartBlock};
    const auto startBlock = attributes->GetValue(startKey, CAmount{});
    if (pindex->nHeight < startBlock) {
        return;
    }

    const auto loanTokens = GetLoanTokensForFutures(cache, *attributes);
    const auto blockPeriod = attributes->GetValue(blockKey, CAmount{});
    const auto futureSwapBlock = (pindex->nHeight - startBlock) % blockPeriod == 0;

    LiquidityForFuturesLimit(pindex, cache, consensus, loanTokens, futureSwapBlock);

    if (!futureSwapBlock) {
        return;
    }

    auto time = GetTimeMillis();
    LogPrintf("Future swap settlement in progress.. (height: %d)\n", pindex->nHeight);

    const auto rewardPct = attributes->GetValue(rewardKey, CAmount{});
    const auto discount{COIN - rewardPct};
    const auto premium{COIN + rewardPct};

    std::map<DCT_ID, CFuturesPrice> futuresPrices;

    for (const auto &[id, loanToken] : loanTokens) {
        const auto useNextPrice{false}, requireLivePrice{true};
        const auto discountPrice =
            cache.GetAmountInCurrency(discount, loanToken.fixedIntervalPriceId, useNextPrice, requireLivePrice);
        const auto premiumPrice =
            cache.GetAmountInCurrency(premium, loanToken.fixedIntervalPriceId, useNextPrice, requireLivePrice);
        if (!discountPrice || !premiumPrice) {
            continue;
        }

        futuresPrices.emplace(id, CFuturesPrice{*discountPrice, *premiumPrice});
    }

    CDataStructureV0 burnKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2203Burned};
    CDataStructureV0 mintedKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2203Minted};

    auto burned = attributes->GetValue(burnKey, CBalances{});
    auto minted = attributes->GetValue(mintedKey, CBalances{});

    std::map<CFuturesUserKey, CFuturesUserValue> unpaidContracts;
    std::set<CFuturesUserKey> deletionPending;

    auto dUsdToTokenSwapsCounter = 0;
    auto tokenTodUsdSwapsCounter = 0;

    cache.ForEachFuturesUserValues(
        [&](const CFuturesUserKey &key, const CFuturesUserValue &futuresValues) {
            CAccountsHistoryWriter view(cache,
                                        pindex->nHeight,
                                        GetNextAccPosition(),
                                        pindex->GetBlockHash(),
                                        uint8_t(CustomTxType::FutureSwapExecution));

            deletionPending.insert(key);

            const auto source = view.GetLoanTokenByID(futuresValues.source.nTokenId);
            assert(source);

            if (source->symbol == "DUSD") {
                const DCT_ID destId{futuresValues.destination};
                const auto destToken = view.GetLoanTokenByID(destId);
                assert(destToken);
                try {
                    const auto &premiumPrice = futuresPrices.at(destId).premium;
                    if (premiumPrice > 0) {
                        const auto total = DivideAmounts(futuresValues.source.nValue, premiumPrice);
                        view.AddMintedTokens(destId, total);
                        CTokenAmount destination{destId, total};
                        view.AddBalance(key.owner, destination);
                        burned.Add(futuresValues.source);
                        minted.Add(destination);
                        dUsdToTokenSwapsCounter++;
                        LogPrint(BCLog::FUTURESWAP,
                                 "ProcessFutures (): Owner %s source %s destination %s\n",
                                 key.owner.GetHex(),
                                 futuresValues.source.ToString(),
                                 destination.ToString());
                    }
                } catch (const std::out_of_range &) {
                    unpaidContracts.emplace(key, futuresValues);
                }

            } else {
                const auto tokenDUSD = view.GetToken("DUSD");
                assert(tokenDUSD);

                try {
                    const auto &discountPrice = futuresPrices.at(futuresValues.source.nTokenId).discount;
                    const auto total = MultiplyAmounts(futuresValues.source.nValue, discountPrice);
                    view.AddMintedTokens(tokenDUSD->first, total);
                    CTokenAmount destination{tokenDUSD->first, total};
                    view.AddBalance(key.owner, destination);
                    burned.Add(futuresValues.source);
                    minted.Add(destination);
                    tokenTodUsdSwapsCounter++;
                    LogPrint(BCLog::FUTURESWAP,
                             "ProcessFutures (): Payment Owner %s source %s destination %s\n",
                             key.owner.GetHex(),
                             futuresValues.source.ToString(),
                             destination.ToString());
                } catch (const std::out_of_range &) {
                    unpaidContracts.emplace(key, futuresValues);
                }
            }

            view.Flush();

            return true;
        },
        {static_cast<uint32_t>(pindex->nHeight), {}, std::numeric_limits<uint32_t>::max()});

    const auto contractAddressValue = GetFutureSwapContractAddress(SMART_CONTRACT_DFIP_2203);
    assert(contractAddressValue);

    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2203Current};

    auto balances = attributes->GetValue(liveKey, CBalances{});

    auto failedContractsCounter = unpaidContracts.size();

    // Refund unpaid contracts
    for (const auto &[key, value] : unpaidContracts) {
        CAccountsHistoryWriter subView(cache,
                                       pindex->nHeight,
                                       GetNextAccPosition(),
                                       pindex->GetBlockHash(),
                                       uint8_t(CustomTxType::FutureSwapRefund));
        subView.SubBalance(*contractAddressValue, value.source);
        subView.Flush();

        CAccountsHistoryWriter addView(cache,
                                       pindex->nHeight,
                                       GetNextAccPosition(),
                                       pindex->GetBlockHash(),
                                       uint8_t(CustomTxType::FutureSwapRefund));
        addView.AddBalance(key.owner, value.source);
        addView.Flush();

        LogPrint(
            BCLog::FUTURESWAP, "%s: Refund Owner %s value %s\n", __func__, key.owner.GetHex(), value.source.ToString());
        balances.Sub(value.source);
    }

    for (const auto &key : deletionPending) {
        cache.EraseFuturesUserValues(key);
    }

    attributes->SetValue(burnKey, std::move(burned));
    attributes->SetValue(mintedKey, std::move(minted));

    if (!unpaidContracts.empty()) {
        attributes->SetValue(liveKey, std::move(balances));
    }

    LogPrintf(
        "Future swap settlement completed: (%d DUSD->Token swaps," /* Continued */
        " %d Token->DUSD swaps, %d refunds (height: %d, time: %dms)\n",
        dUsdToTokenSwapsCounter,
        tokenTodUsdSwapsCounter,
        failedContractsCounter,
        pindex->nHeight,
        GetTimeMillis() - time);

    cache.SetVariable(*attributes);
}

static void ProcessGovEvents(const CBlockIndex *pindex,
                             CCustomCSView &cache,
                             const Consensus::Params &consensus,
                             const std::shared_ptr<CScopedTemplate> &evmTemplate) {
    if (pindex->nHeight < consensus.DF11FortCanningHeight) {
        return;
    }

    // Apply any pending GovVariable changes. Will come into effect on the next block.
    auto storedGovVars = cache.GetStoredVariables(pindex->nHeight);
    for (const auto &var : storedGovVars) {
        if (var) {
            CCustomCSView govCache(cache);
            // Add to existing ATTRIBUTES instead of overwriting.
            if (var->GetName() == "ATTRIBUTES") {
                auto govVar = cache.GetAttributes();
                govVar->time = pindex->GetBlockTime();
                govVar->evmTemplate = evmTemplate;
                auto newVar = std::dynamic_pointer_cast<ATTRIBUTES>(var);
                assert(newVar);

                CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Foundation, DFIPKeys::Members};
                auto memberRemoval = newVar->GetValue(key, std::set<std::string>{});

                if (!memberRemoval.empty()) {
                    auto existingMembers = govVar->GetValue(key, std::set<CScript>{});

                    for (auto &member : memberRemoval) {
                        if (member.empty()) {
                            continue;
                        }

                        if (member[0] == '-') {
                            auto memberCopy{member};
                            const auto dest = DecodeDestination(memberCopy.erase(0, 1));
                            if (!IsValidDestination(dest)) {
                                continue;
                            }
                            existingMembers.erase(GetScriptForDestination(dest));

                        } else {
                            const auto dest = DecodeDestination(member);
                            if (!IsValidDestination(dest)) {
                                continue;
                            }
                            existingMembers.insert(GetScriptForDestination(dest));
                        }
                    }

                    govVar->SetValue(key, existingMembers);

                    // Remove this key and apply any other changes
                    newVar->EraseKey(key);
                    if (govVar->Import(newVar->Export()) && govVar->Validate(govCache) &&
                        govVar->Apply(govCache, pindex->nHeight) && govCache.SetVariable(*govVar)) {
                        govCache.Flush();
                    }
                } else {
                    if (govVar->Import(var->Export()) && govVar->Validate(govCache) &&
                        govVar->Apply(govCache, pindex->nHeight) && govCache.SetVariable(*govVar)) {
                        govCache.Flush();
                    }
                }
            } else if (var->Validate(govCache) && var->Apply(govCache, pindex->nHeight) && govCache.SetVariable(*var)) {
                govCache.Flush();
            }
        }
    }
    cache.EraseStoredVariables(static_cast<uint32_t>(pindex->nHeight));
}

static bool ApplyGovVars(CCustomCSView &cache,
                         const CBlockIndex &pindex,
                         const std::map<std::string, std::string> &attrs) {
    if (auto govVar = cache.GetVariable("ATTRIBUTES")) {
        if (auto var = dynamic_cast<ATTRIBUTES *>(govVar.get())) {
            var->time = pindex.nTime;

            UniValue obj(UniValue::VOBJ);
            for (const auto &[key, value] : attrs) {
                obj.pushKV(key, value);
            }

            if (var->Import(obj) && var->Validate(cache) && var->Apply(cache, pindex.nHeight) &&
                cache.SetVariable(*var)) {
                return true;
            }
        }
    }

    return false;
}

static void ProcessTokenToGovVar(const CBlockIndex *pindex, CCustomCSView &cache, const Consensus::Params &consensus) {
    // Migrate at +1 height so that GetLastHeight() in Gov var
    // Validate() has a height equal to the GW fork.
    if (pindex->nHeight != consensus.DF16FortCanningCrunchHeight + 1) {
        return;
    }

    auto time = GetTimeMillis();
    LogPrintf("Token attributes migration in progress.. (height: %d)\n", pindex->nHeight);

    std::map<DCT_ID, CLoanSetLoanToken> loanTokens;
    std::vector<CLoanSetCollateralTokenImplementation> collateralTokens;

    cache.ForEachLoanToken([&](const DCT_ID &key, const CLoanSetLoanToken &loanToken) {
        loanTokens[key] = loanToken;
        return true;
    });

    cache.ForEachLoanCollateralToken([&](const CollateralTokenKey &key, const uint256 &collTokenTx) {
        auto collToken = cache.GetLoanCollateralToken(collTokenTx);
        if (collToken) {
            collateralTokens.push_back(*collToken);
        }
        return true;
    });

    // Apply fixed_interval_price_id first
    std::map<std::string, std::string> attrsFirst;
    std::map<std::string, std::string> attrsSecond;

    int loanCount = 0, collateralCount = 0;

    try {
        for (const auto &[id, token] : loanTokens) {
            std::string prefix = KeyBuilder(ATTRIBUTES::displayVersions().at(VersionTypes::v0),
                                            ATTRIBUTES::displayTypes().at(AttributeTypes::Token),
                                            id.v);
            attrsFirst[KeyBuilder(
                prefix, ATTRIBUTES::displayKeys().at(AttributeTypes::Token).at(TokenKeys::FixedIntervalPriceId))] =
                token.fixedIntervalPriceId.first + '/' + token.fixedIntervalPriceId.second;
            attrsSecond[KeyBuilder(
                prefix, ATTRIBUTES::displayKeys().at(AttributeTypes::Token).at(TokenKeys::LoanMintingEnabled))] =
                token.mintable ? "true" : "false";
            attrsSecond[KeyBuilder(
                prefix, ATTRIBUTES::displayKeys().at(AttributeTypes::Token).at(TokenKeys::LoanMintingInterest))] =
                KeyBuilder(ValueFromAmount(token.interest).get_real());
            ++loanCount;
        }

        for (const auto &token : collateralTokens) {
            std::string prefix = KeyBuilder(ATTRIBUTES::displayVersions().at(VersionTypes::v0),
                                            ATTRIBUTES::displayTypes().at(AttributeTypes::Token),
                                            token.idToken.v);
            attrsFirst[KeyBuilder(
                prefix, ATTRIBUTES::displayKeys().at(AttributeTypes::Token).at(TokenKeys::FixedIntervalPriceId))] =
                token.fixedIntervalPriceId.first + '/' + token.fixedIntervalPriceId.second;
            attrsSecond[KeyBuilder(
                prefix, ATTRIBUTES::displayKeys().at(AttributeTypes::Token).at(TokenKeys::LoanCollateralEnabled))] =
                "true";
            attrsSecond[KeyBuilder(
                prefix, ATTRIBUTES::displayKeys().at(AttributeTypes::Token).at(TokenKeys::LoanCollateralFactor))] =
                KeyBuilder(ValueFromAmount(token.factor).get_real());
            ++collateralCount;
        }

        CCustomCSView govCache(cache);
        if (ApplyGovVars(govCache, *pindex, attrsFirst) && ApplyGovVars(govCache, *pindex, attrsSecond)) {
            govCache.Flush();

            // Erase old tokens afterwards to avoid invalid state during transition
            for (const auto &item : loanTokens) {
                cache.EraseLoanToken(item.first);
            }

            for (const auto &token : collateralTokens) {
                cache.EraseLoanCollateralToken(token);
            }
        }

        LogPrintf(
            "Token attributes migration complete: " /* Continued */
            "(%d loan tokens, %d collateral tokens, height: %d, time: %dms)\n",
            loanCount,
            collateralCount,
            pindex->nHeight,
            GetTimeMillis() - time);

    } catch (std::out_of_range &) {
        LogPrintf("Non-existant map entry referenced in loan/collateral token to Gov var migration\n");
    }
}

template <typename T>
static inline T CalculateNewAmount(const CAmount multiplier, const T amount) {
    return multiplier < 0 ? DivideAmounts(amount, std::abs(multiplier)) : MultiplyAmounts(amount, multiplier);
}

template <typename T>
static inline T CalculateNewAmount(const int32_t multiplier, const T amount) {
    return multiplier < 0 ? amount / std::abs(multiplier) : amount * multiplier;
}

size_t RewardConsolidationWorkersCount() {
    const size_t workersMax = GetNumCores() - 1;
    return workersMax > 2 ? workersMax : 3;
}

// Note: Be careful with lambda captures and default args. GCC 11.2.0, appears the if the captures are
// unused in the function directly, but inside the lambda, it completely disassociates them from the fn
// possibly when the lambda is lifted up and with default args, ends up inling the default arg
// completely. TODO: verify with smaller test case.
// But scenario: If `interruptOnShutdown` is set as default arg to false, it will never be set true
// on the below as it's inlined by gcc 11.2.0 on Ubuntu 22.04 incorrectly. Behavior is correct
// in lower versions of gcc or across clang.
void ConsolidateRewards(CCustomCSView &view,
                        int height,
                        const std::vector<std::pair<CScript, CAmount>> &items,
                        bool interruptOnShutdown,
                        int numWorkers) {
    int nWorkers = numWorkers < 1 ? RewardConsolidationWorkersCount() : numWorkers;
    auto rewardsTime = GetTimeMicros();
    boost::asio::thread_pool workerPool(nWorkers);
    boost::asio::thread_pool mergeWorker(1);
    std::atomic<uint64_t> tasksCompleted{0};
    std::atomic<uint64_t> reportedTs{0};

    for (auto &[owner, amount] : items) {
        // See https://github.com/DeFiCh/ain/pull/1291
        // https://github.com/DeFiCh/ain/pull/1291#issuecomment-1137638060
        // Technically not fully synchronized, but avoid races
        // due to the segregated areas of operation.
        boost::asio::post(workerPool, [&, &account = owner]() {
            if (interruptOnShutdown && ShutdownRequested()) {
                return;
            }
            auto tempView = std::make_unique<CCustomCSView>(view);
            tempView->CalculateOwnerRewards(account, height);

            boost::asio::post(mergeWorker, [&, tempView = std::move(tempView)]() {
                if (interruptOnShutdown && ShutdownRequested()) {
                    return;
                }
                tempView->Flush();

                // This entire block is already serialized with single merge worker.
                // So, relaxed ordering is more than sufficient - don't even need
                // atomics really.
                auto itemsCompleted = tasksCompleted.fetch_add(1, std::memory_order::memory_order_relaxed);
                const auto logTimeIntervalMillis = 3 * 1000;
                if (GetTimeMillis() - reportedTs > logTimeIntervalMillis) {
                    LogPrintf("Reward consolidation: %.2f%% completed (%d/%d)\n",
                              (itemsCompleted * 1.f / items.size()) * 100.0,
                              itemsCompleted,
                              items.size());
                    reportedTs.store(GetTimeMillis(), std::memory_order::memory_order_relaxed);
                }
            });
        });
    }
    workerPool.join();
    mergeWorker.join();

    auto itemsCompleted = tasksCompleted.load();
    LogPrintf("Reward consolidation: 100%% completed (%d/%d, time: %dms)\n",
              itemsCompleted,
              itemsCompleted,
              MILLI * (GetTimeMicros() - rewardsTime));
}

template <typename GovVar>
static Res UpdateLiquiditySplits(CCustomCSView &view,
                                 const DCT_ID oldPoolId,
                                 const DCT_ID newPoolId,
                                 const uint32_t height) {
    if (auto var = view.GetVariable(GovVar::TypeName())) {
        if (auto lpVar = std::dynamic_pointer_cast<GovVar>(var)) {
            if (lpVar->splits.count(oldPoolId) > 0) {
                const auto value = lpVar->splits[oldPoolId];
                lpVar->splits.erase(oldPoolId);
                lpVar->splits[newPoolId] = value;
                lpVar->Apply(view, height);
                view.SetVariable(*lpVar);
            }
        }
    } else {
        return Res::Err("Failed to get %s", LP_SPLITS::TypeName());
    }

    return Res::Ok();
}

template <typename T>
static Res PoolSplits(CCustomCSView &view,
                      CAmount &totalBalance,
                      ATTRIBUTES &attributes,
                      const DCT_ID oldTokenId,
                      const DCT_ID newTokenId,
                      const CBlockIndex *pindex,
                      const CreationTxs &creationTxs,
                      const T multiplier) {
    LogPrintf(
        "Pool migration in progress.. (token %d -> %d, height: %d)\n", oldTokenId.v, newTokenId.v, pindex->nHeight);

    try {
        assert(creationTxs.count(oldTokenId.v));
        for (const auto &[oldPoolId, creationTx] : creationTxs.at(oldTokenId.v).second) {
            auto loopTime = GetTimeMillis();
            auto oldPoolToken = view.GetToken(oldPoolId);
            if (!oldPoolToken) {
                throw std::runtime_error(strprintf("Failed to get related pool token: %d", oldPoolId.v));
            }

            CTokenImplementation newPoolToken{*oldPoolToken};
            newPoolToken.creationHeight = pindex->nHeight;
            newPoolToken.creationTx = creationTx;
            newPoolToken.minted = 0;

            size_t suffixCount{1};
            view.ForEachPoolPair([&](DCT_ID const &poolId, const CPoolPair &pool) {
                const auto tokenA = view.GetToken(pool.idTokenA);
                const auto tokenB = view.GetToken(pool.idTokenB);
                assert(tokenA);
                assert(tokenB);
                if ((tokenA->destructionHeight != -1 && tokenA->destructionTx != uint256{}) ||
                    (tokenB->destructionHeight != -1 && tokenB->destructionTx != uint256{})) {
                    const auto poolToken = view.GetToken(poolId);
                    assert(poolToken);
                    if (poolToken->symbol.find(oldPoolToken->symbol + "/v") != std::string::npos) {
                        ++suffixCount;
                    }
                }
                return true;
            });

            oldPoolToken->symbol += "/v" + std::to_string(suffixCount);
            oldPoolToken->flags |= static_cast<uint8_t>(CToken::TokenFlags::Tradeable);
            oldPoolToken->destructionHeight = pindex->nHeight;
            oldPoolToken->destructionTx = pindex->GetBlockHash();

            // EVM Template will be null so no DST20 will be updated or created
            BlockContext dummyContext{std::numeric_limits<uint32_t>::max(), {}, Params().GetConsensus()};
            UpdateTokenContext ctx{*oldPoolToken, dummyContext, false, true, false};
            auto res = view.UpdateToken(ctx);
            if (!res) {
                throw std::runtime_error(res.msg);
            }

            auto resVal = view.CreateToken(newPoolToken, dummyContext);
            if (!resVal) {
                throw std::runtime_error(resVal.msg);
            }

            const DCT_ID newPoolId{resVal.val->v};

            auto oldPoolPair = view.GetPoolPair(oldPoolId);
            if (!oldPoolPair) {
                throw std::runtime_error(strprintf("Failed to get related pool: %d", oldPoolId.v));
            }

            LogPrintf("Pool migration: Old pair (id: %d, token a: %d, b: %d, reserve a: %d, b: %d, liquidity: %d)\n",
                      oldPoolId.v,
                      oldPoolPair->idTokenA.v,
                      oldPoolPair->idTokenB.v,
                      oldPoolPair->reserveA,
                      oldPoolPair->reserveB,
                      oldPoolPair->totalLiquidity);

            CPoolPair newPoolPair{*oldPoolPair};
            if (oldPoolPair->idTokenA == oldTokenId) {
                newPoolPair.idTokenA = newTokenId;
            } else {
                newPoolPair.idTokenB = newTokenId;
            }
            newPoolPair.creationTx = newPoolToken.creationTx;
            newPoolPair.creationHeight = pindex->nHeight;
            newPoolPair.reserveA = 0;
            newPoolPair.reserveB = 0;
            newPoolPair.totalLiquidity = 0;

            res = view.SetPoolPair(newPoolId, pindex->nHeight, newPoolPair);
            if (!res) {
                throw std::runtime_error(strprintf("SetPoolPair on new pool pair: %s", res.msg));
            }

            std::vector<std::pair<CScript, CAmount>> balancesToMigrate;
            uint64_t totalAccounts = 0;
            view.ForEachBalance([&, oldPoolId = oldPoolId](const CScript &owner, CTokenAmount balance) {
                if (oldPoolId.v == balance.nTokenId.v && balance.nValue > 0) {
                    balancesToMigrate.emplace_back(owner, balance.nValue);
                }
                totalAccounts++;
                return true;
            });

            auto nWorkers = RewardConsolidationWorkersCount();
            LogPrintf("Pool migration: Consolidating rewards (count: %d, total: %d, concurrency: %d)..\n",
                      balancesToMigrate.size(),
                      totalAccounts,
                      nWorkers);

            // Largest first to make sure we are over MINIMUM_LIQUIDITY on first call to AddLiquidity
            std::sort(balancesToMigrate.begin(),
                      balancesToMigrate.end(),
                      [](const std::pair<CScript, CAmount> &a, const std::pair<CScript, CAmount> &b) {
                          return a.second > b.second;
                      });

            ConsolidateRewards(view, pindex->nHeight, balancesToMigrate, false, nWorkers);

            // Special case. No liquidity providers in a previously used pool.
            if (balancesToMigrate.empty() && oldPoolPair->totalLiquidity == CPoolPair::MINIMUM_LIQUIDITY) {
                balancesToMigrate.emplace_back(Params().GetConsensus().burnAddress,
                                               CAmount{CPoolPair::MINIMUM_LIQUIDITY});
            }

            for (auto &[owner, amount] : balancesToMigrate) {
                if (owner != Params().GetConsensus().burnAddress) {
                    CAccountsHistoryWriter subView(view,
                                                   pindex->nHeight,
                                                   GetNextAccPosition(),
                                                   pindex->GetBlockHash(),
                                                   uint8_t(CustomTxType::TokenSplit));

                    res = subView.SubBalance(owner, CTokenAmount{oldPoolId, amount});
                    if (!res.ok) {
                        throw std::runtime_error(strprintf("SubBalance failed: %s", res.msg));
                    }
                    subView.Flush();
                }

                if (oldPoolPair->totalLiquidity < CPoolPair::MINIMUM_LIQUIDITY) {
                    throw std::runtime_error("totalLiquidity less than minimum.");
                }

                // First deposit to the pool has MINIMUM_LIQUIDITY removed and does not
                // belong to anyone. Give this to the last person leaving the pool.
                if (oldPoolPair->totalLiquidity - amount == CPoolPair::MINIMUM_LIQUIDITY) {
                    amount += CPoolPair::MINIMUM_LIQUIDITY;
                }

                CAmount resAmountA =
                    (arith_uint256(amount) * oldPoolPair->reserveA / oldPoolPair->totalLiquidity).GetLow64();
                CAmount resAmountB =
                    (arith_uint256(amount) * oldPoolPair->reserveB / oldPoolPair->totalLiquidity).GetLow64();
                oldPoolPair->reserveA -= resAmountA;
                oldPoolPair->reserveB -= resAmountB;
                oldPoolPair->totalLiquidity -= amount;

                CAmount amountA{0}, amountB{0};
                if (oldPoolPair->idTokenA == oldTokenId) {
                    amountA = CalculateNewAmount(multiplier, resAmountA);
                    totalBalance += amountA;
                    amountB = resAmountB;
                } else {
                    amountA = resAmountA;
                    amountB = CalculateNewAmount(multiplier, resAmountB);
                    totalBalance += amountB;
                }

                CAccountsHistoryWriter addView(view,
                                               pindex->nHeight,
                                               GetNextAccPosition(),
                                               pindex->GetBlockHash(),
                                               uint8_t(CustomTxType::TokenSplit));

                auto refundBalances = [&, owner = owner]() {
                    addView.AddBalance(owner, {newPoolPair.idTokenA, amountA});
                    addView.AddBalance(owner, {newPoolPair.idTokenB, amountB});
                    addView.Flush();
                };

                if (amountA <= 0 || amountB <= 0 || owner == Params().GetConsensus().burnAddress) {
                    refundBalances();
                    continue;
                }

                CAmount liquidity{0};
                if (newPoolPair.totalLiquidity == 0) {
                    liquidity = (arith_uint256(amountA) * amountB).sqrt().GetLow64();
                    liquidity -= CPoolPair::MINIMUM_LIQUIDITY;
                    newPoolPair.totalLiquidity = CPoolPair::MINIMUM_LIQUIDITY;
                } else {
                    CAmount liqA =
                        (arith_uint256(amountA) * newPoolPair.totalLiquidity / newPoolPair.reserveA).GetLow64();
                    CAmount liqB =
                        (arith_uint256(amountB) * newPoolPair.totalLiquidity / newPoolPair.reserveB).GetLow64();
                    liquidity = std::min(liqA, liqB);

                    if (liquidity == 0) {
                        refundBalances();
                        continue;
                    }
                }

                auto resTotal = SafeAdd(newPoolPair.totalLiquidity, liquidity);
                if (!resTotal) {
                    refundBalances();
                    continue;
                }
                newPoolPair.totalLiquidity = resTotal;

                auto resA = SafeAdd(newPoolPair.reserveA, amountA);
                auto resB = SafeAdd(newPoolPair.reserveB, amountB);
                if (resA && resB) {
                    newPoolPair.reserveA = resA;
                    newPoolPair.reserveB = resB;
                } else {
                    refundBalances();
                    continue;
                }

                res = addView.AddBalance(owner, {newPoolId, liquidity});
                if (!res) {
                    refundBalances();
                    continue;
                }
                addView.Flush();

                auto oldPoolLogStr = CTokenAmount{oldPoolId, amount}.ToString();
                auto newPoolLogStr = CTokenAmount{newPoolId, liquidity}.ToString();
                LogPrint(BCLog::TOKENSPLIT,
                         "TokenSplit: LP (%s: %s => %s)\n",
                         ScriptToString(owner),
                         oldPoolLogStr,
                         newPoolLogStr);

                view.SetShare(newPoolId, owner, pindex->nHeight);
            }

            DCT_ID maxToken{std::numeric_limits<uint32_t>::max()};
            if (oldPoolPair->idTokenA == oldTokenId) {
                view.EraseDexFeePct(oldPoolPair->idTokenA, maxToken);
                view.EraseDexFeePct(maxToken, oldPoolPair->idTokenA);
            } else {
                view.EraseDexFeePct(oldPoolPair->idTokenB, maxToken);
                view.EraseDexFeePct(maxToken, oldPoolPair->idTokenB);
            }

            view.EraseDexFeePct(oldPoolId, oldPoolPair->idTokenA);
            view.EraseDexFeePct(oldPoolId, oldPoolPair->idTokenB);

            if (oldPoolPair->totalLiquidity != 0) {
                throw std::runtime_error(
                    strprintf("totalLiquidity should be zero. Remainder: %d", oldPoolPair->totalLiquidity));
            }

            LogPrintf("Pool migration: New pair (id: %d, token a: %d, b: %d, reserve a: %d, b: %d, liquidity: %d)\n",
                      newPoolId.v,
                      newPoolPair.idTokenA.v,
                      newPoolPair.idTokenB.v,
                      newPoolPair.reserveA,
                      newPoolPair.reserveB,
                      newPoolPair.totalLiquidity);

            res = view.SetPoolPair(newPoolId, pindex->nHeight, newPoolPair);
            if (!res) {
                throw std::runtime_error(strprintf("SetPoolPair on new pool pair: %s", res.msg));
            }

            res = view.SetPoolPair(oldPoolId, pindex->nHeight, *oldPoolPair);
            if (!res) {
                throw std::runtime_error(strprintf("SetPoolPair on old pool pair: %s", res.msg));
            }

            res = view.UpdatePoolPair(oldPoolId, pindex->nHeight, false, -1, CScript{}, CBalances{});
            if (!res) {
                throw std::runtime_error(strprintf("UpdatePoolPair on old pool pair: %s", res.msg));
            }

            std::vector<CDataStructureV0> eraseKeys;
            for (const auto &[key, value] : attributes.GetAttributesMap()) {
                if (const auto v0Key = std::get_if<CDataStructureV0>(&key);
                    v0Key->type == AttributeTypes::Poolpairs && v0Key->typeId == oldPoolId.v) {
                    CDataStructureV0 newKey{AttributeTypes::Poolpairs, newPoolId.v, v0Key->key, v0Key->keyId};
                    attributes.SetValue(newKey, value);
                    eraseKeys.push_back(*v0Key);
                }
            }

            for (const auto &key : eraseKeys) {
                attributes.EraseKey(key);
            }

            res = UpdateLiquiditySplits<LP_SPLITS>(view, oldPoolId, newPoolId, pindex->nHeight);
            if (!res) {
                throw std::runtime_error(res.msg);
            }

            res = UpdateLiquiditySplits<LP_LOAN_TOKEN_SPLITS>(view, oldPoolId, newPoolId, pindex->nHeight);
            if (!res) {
                throw std::runtime_error(res.msg);
            }
            LogPrintf("Pool migration complete: (%d -> %d, height: %d, time: %dms)\n",
                      oldPoolId.v,
                      newPoolId.v,
                      pindex->nHeight,
                      GetTimeMillis() - loopTime);
        }

    } catch (const std::runtime_error &e) {
        return Res::Err(e.what());
    }
    return Res::Ok();
}

template <typename T>
static Res VaultSplits(CCustomCSView &view,
                       ATTRIBUTES &attributes,
                       const DCT_ID oldTokenId,
                       const DCT_ID newTokenId,
                       const int height,
                       const T multiplier) {
    auto time = GetTimeMillis();
    LogPrintf("Vaults rebalance in progress.. (token %d -> %d, height: %d)\n", oldTokenId.v, newTokenId.v, height);

    std::vector<std::pair<CVaultId, CAmount>> loanTokenAmounts;
    view.ForEachLoanTokenAmount([&](const CVaultId &vaultId, const CBalances &balances) {
        for (const auto &[tokenId, amount] : balances.balances) {
            if (tokenId == oldTokenId) {
                loanTokenAmounts.emplace_back(vaultId, amount);
            }
        }
        return true;
    });

    for (auto &[vaultId, amount] : loanTokenAmounts) {
        const auto res = view.SubLoanToken(vaultId, {oldTokenId, amount});
        if (!res) {
            return res;
        }
    }

    CVaultId failedVault;
    std::vector<std::tuple<CVaultId, CInterestRateV3, std::string>> loanInterestRates;
    if (height >= Params().GetConsensus().DF18FortCanningGreatWorldHeight) {
        view.ForEachVaultInterestV3([&](const CVaultId &vaultId, DCT_ID tokenId, const CInterestRateV3 &rate) {
            if (tokenId == oldTokenId) {
                const auto vaultData = view.GetVault(vaultId);
                if (!vaultData) {
                    failedVault = vaultId;
                    return false;
                }
                loanInterestRates.emplace_back(vaultId, rate, vaultData->schemeId);
            }
            return true;
        });
    } else {
        view.ForEachVaultInterestV2([&](const CVaultId &vaultId, DCT_ID tokenId, const CInterestRateV2 &rate) {
            if (tokenId == oldTokenId) {
                const auto vaultData = view.GetVault(vaultId);
                if (!vaultData) {
                    failedVault = vaultId;
                    return false;
                }
                loanInterestRates.emplace_back(vaultId, ConvertInterestRateToV3(rate), vaultData->schemeId);
            }
            return true;
        });
    }

    if (failedVault != CVaultId{}) {
        return Res::Err("Failed to get vault data for: %s", failedVault.ToString());
    }

    attributes.EraseKey(CDataStructureV0{AttributeTypes::Locks, ParamIDs::TokenID, oldTokenId.v});
    attributes.SetValue(CDataStructureV0{AttributeTypes::Locks, ParamIDs::TokenID, newTokenId.v}, true);

    if (auto res = attributes.Apply(view, height); !res) {
        return res;
    }
    view.SetVariable(attributes);

    for (const auto &[vaultId, amount] : loanTokenAmounts) {
        auto newAmount = CalculateNewAmount(multiplier, amount);

        auto oldTokenAmount = CTokenAmount{oldTokenId, amount};
        auto newTokenAmount = CTokenAmount{newTokenId, newAmount};

        LogPrint(BCLog::TOKENSPLIT,
                 "TokenSplit: V Loan (%s: %s => %s)\n",
                 vaultId.ToString(),
                 oldTokenAmount.ToString(),
                 newTokenAmount.ToString());

        if (auto res = view.AddLoanToken(vaultId, newTokenAmount); !res) {
            return res;
        }

        if (const auto vault = view.GetVault(vaultId)) {
            VaultHistoryKey subKey{static_cast<uint32_t>(height), vaultId, GetNextAccPosition(), vault->ownerAddress};
            VaultHistoryValue subValue{
                uint256{}, static_cast<uint8_t>(CustomTxType::TokenSplit), {{oldTokenId, -amount}}};
            view.GetHistoryWriters().WriteVaultHistory(subKey, subValue);

            VaultHistoryKey addKey{static_cast<uint32_t>(height), vaultId, GetNextAccPosition(), vault->ownerAddress};
            VaultHistoryValue addValue{
                uint256{}, static_cast<uint8_t>(CustomTxType::TokenSplit), {{newTokenId, newAmount}}};
            view.GetHistoryWriters().WriteVaultHistory(addKey, addValue);
        }
    }

    const auto loanToken = view.GetLoanTokenByID(newTokenId);
    if (!loanToken) {
        return Res::Err("Failed to get loan token.");
    }

    // Pre-populate to save repeated calls to get loan scheme
    std::map<std::string, CAmount> loanSchemes;
    view.ForEachLoanScheme([&](const std::string &key, const CLoanSchemeData &data) {
        loanSchemes.emplace(key, data.rate);
        return true;
    });

    for (auto &[vaultId, rate, schemeId] : loanInterestRates) {
        CAmount loanSchemeRate{0};
        try {
            loanSchemeRate = loanSchemes.at(schemeId);
        } catch (const std::out_of_range &) {
            return Res::Err("Failed to get loan scheme.");
        }

        view.EraseInterest(vaultId, oldTokenId, height);
        auto oldRateToHeight = rate.interestToHeight;
        auto newRateToHeight = CalculateNewAmount(multiplier, rate.interestToHeight.amount);

        rate.interestToHeight.amount = newRateToHeight;

        auto oldInterestPerBlock = rate.interestPerBlock;
        CInterestAmount newInterestRatePerBlock{};

        auto amounts = view.GetLoanTokens(vaultId);
        if (amounts) {
            newInterestRatePerBlock =
                InterestPerBlockCalculationV3(amounts->balances[newTokenId], loanToken->interest, loanSchemeRate);
            rate.interestPerBlock = newInterestRatePerBlock;
        }

        if (LogAcceptCategory(BCLog::TOKENSPLIT)) {
            LogPrint(BCLog::TOKENSPLIT,
                     "TokenSplit: V Interest (%s: %s => %s, %s => %s)\n",
                     vaultId.ToString(),
                     GetInterestPerBlockHighPrecisionString(oldRateToHeight),
                     GetInterestPerBlockHighPrecisionString({oldRateToHeight.negative, newRateToHeight}),
                     GetInterestPerBlockHighPrecisionString(oldInterestPerBlock),
                     GetInterestPerBlockHighPrecisionString(newInterestRatePerBlock));
        }

        view.WriteInterestRate(std::make_pair(vaultId, newTokenId), rate, rate.height);
    }

    std::vector<std::pair<CVaultView::AuctionStoreKey, CAuctionBatch>> auctionBatches;
    view.ForEachAuctionBatch([&](const CVaultView::AuctionStoreKey &key, const CAuctionBatch &value) {
        if (value.loanAmount.nTokenId == oldTokenId || value.collaterals.balances.count(oldTokenId)) {
            auctionBatches.emplace_back(key, value);
        }
        return true;
    });

    for (auto &[key, value] : auctionBatches) {
        view.EraseAuctionBatch(key);

        if (value.loanAmount.nTokenId == oldTokenId) {
            auto oldLoanAmount = value.loanAmount;
            auto oldInterest = value.loanInterest;

            auto newLoanAmount = CTokenAmount{newTokenId, CalculateNewAmount(multiplier, value.loanAmount.nValue)};
            value.loanAmount.nTokenId = newLoanAmount.nTokenId;
            value.loanAmount.nValue = newLoanAmount.nValue;

            auto newLoanInterest = CalculateNewAmount(multiplier, value.loanInterest);
            value.loanInterest = newLoanInterest;

            LogPrint(BCLog::TOKENSPLIT,
                     "TokenSplit: V AuctionL (%s,%d: %s => %s, %d => %d)\n",
                     key.first.ToString(),
                     key.second,
                     oldLoanAmount.ToString(),
                     newLoanAmount.ToString(),
                     oldInterest,
                     newLoanInterest);
        }

        if (value.collaterals.balances.count(oldTokenId)) {
            auto oldAmount = CTokenAmount{oldTokenId, value.collaterals.balances[oldTokenId]};
            auto newAmount = CTokenAmount{newTokenId, CalculateNewAmount(multiplier, oldAmount.nValue)};

            value.collaterals.balances[newAmount.nTokenId] = newAmount.nValue;
            value.collaterals.balances.erase(oldAmount.nTokenId);

            LogPrint(BCLog::TOKENSPLIT,
                     "TokenSplit: V AuctionC (%s,%d: %s => %s)\n",
                     key.first.ToString(),
                     key.second,
                     oldAmount.ToString(),
                     newAmount.ToString());
        }

        view.StoreAuctionBatch(key, value);
    }

    std::vector<std::pair<CVaultView::AuctionStoreKey, CVaultView::COwnerTokenAmount>> auctionBids;
    view.ForEachAuctionBid([&](const CVaultView::AuctionStoreKey &key, const CVaultView::COwnerTokenAmount &value) {
        if (value.second.nTokenId == oldTokenId) {
            auctionBids.emplace_back(key, value);
        }
        return true;
    });

    for (auto &[key, value] : auctionBids) {
        view.EraseAuctionBid(key);

        auto oldTokenAmount = value.second;
        auto newTokenAmount = CTokenAmount{newTokenId, CalculateNewAmount(multiplier, oldTokenAmount.nValue)};

        value.second = newTokenAmount;

        view.StoreAuctionBid(key, value);

        LogPrint(BCLog::TOKENSPLIT,
                 "TokenSplit: V Bid (%s,%d: %s => %s)\n",
                 key.first.ToString(),
                 key.second,
                 oldTokenAmount.ToString(),
                 newTokenAmount.ToString());
    }

    LogPrintf("Vaults rebalance completed: (token %d -> %d, height: %d, time: %dms)\n",
              oldTokenId.v,
              newTokenId.v,
              height,
              GetTimeMillis() - time);

    return Res::Ok();
}

template <typename T>
static void MigrateV1Remnants(const CCustomCSView &cache,
                              ATTRIBUTES &attributes,
                              const uint8_t key,
                              const DCT_ID oldId,
                              const DCT_ID newId,
                              const T multiplier,
                              const uint8_t typeID = ParamIDs::Economy) {
    CDataStructureV0 attrKey{AttributeTypes::Live, typeID, key};
    auto balances = attributes.GetValue(attrKey, CBalances{});
    for (auto it = balances.balances.begin(); it != balances.balances.end(); ++it) {
        const auto &[tokenId, amount] = *it;
        if (tokenId != oldId) {
            continue;
        }
        balances.balances.erase(it);
        balances.Add({newId, CalculateNewAmount(multiplier, amount)});
        break;
    }
    attributes.SetValue(attrKey, balances);
}

Res GetTokenSuffix(const CCustomCSView &view, const ATTRIBUTES &attributes, const uint32_t id, std::string &newSuffix) {
    CDataStructureV0 ascendantKey{AttributeTypes::Token, id, TokenKeys::Ascendant};
    if (attributes.CheckKey(ascendantKey)) {
        const auto &[previousID, str] =
            attributes.GetValue(ascendantKey, AscendantValue{std::numeric_limits<uint32_t>::max(), ""});
        auto previousToken = view.GetToken(DCT_ID{previousID});
        if (!previousToken) {
            return Res::Err("Previous token %d not found\n", id);
        }

        const auto found = previousToken->symbol.find(newSuffix);
        if (found == std::string::npos) {
            return Res::Err("Previous token name not valid: %s\n", previousToken->symbol);
        }

        const auto versionNumber = previousToken->symbol.substr(found + newSuffix.size());
        uint32_t previousVersion{};
        try {
            previousVersion = std::stoi(versionNumber);
        } catch (...) {
            return Res::Err("Previous token name not valid.");
        }

        newSuffix += std::to_string(++previousVersion);
    } else {
        newSuffix += '1';
    }

    return Res::Ok();
}

template <typename T>
static void UpdateOracleSplitKeys(const uint32_t id, ATTRIBUTES &attributes) {
    std::map<CDataStructureV0, T> updateAttributesKeys;
    attributes.ForEach(
        [&](const CDataStructureV0 &attr, const CAttributeValue &value) {
            if (attr.type != AttributeTypes::Oracles) {
                return false;
            }

            if (attr.typeId != OracleIDs::Splits) {
                return true;
            }

            if (attr.key == OracleKeys::FractionalSplits) {
                return true;
            }

            if (const auto splitMap = std::get_if<T>(&value)) {
                for (auto [splitMapKey, splitMapValue] : *splitMap) {
                    if (splitMapKey == id) {
                        auto copyMap{*splitMap};
                        copyMap.erase(splitMapKey);
                        updateAttributesKeys.emplace(attr, copyMap);
                        break;
                    }
                }
            }

            return true;
        },
        CDataStructureV0{AttributeTypes::Oracles});

    for (const auto &[key, value] : updateAttributesKeys) {
        if (value.empty()) {
            attributes.EraseKey(key);
        } else {
            attributes.SetValue(key, value);
        }
    }
}

template <typename T>
static void ExecuteTokenSplits(const CBlockIndex *pindex,
                               CCustomCSView &cache,
                               const CreationTxs &creationTxs,
                               const Consensus::Params &consensus,
                               ATTRIBUTES &attributes,
                               const T &splits,
                               BlockContext &blockCtx) {
    for (const auto &[id, multiplier] : splits) {
        auto time = GetTimeMillis();
        LogPrintf("Token split in progress.. (id: %d, mul: %d, height: %d)\n", id, multiplier, pindex->nHeight);

        if (!cache.AreTokensLocked({id})) {
            LogPrintf("Token split failed. No locks.\n");
            continue;
        }

        auto view{cache};

        // Refund affected future swaps
        auto res = attributes.RefundFuturesContracts(view, std::numeric_limits<uint32_t>::max(), id);
        if (!res) {
            LogPrintf("Token split failed on refunding futures: %s\n", res.msg);
            continue;
        }

        const DCT_ID oldTokenId{id};

        auto token = view.GetToken(oldTokenId);
        if (!token) {
            LogPrintf("Token split failed. Token %d not found\n", oldTokenId.v);
            continue;
        }

        std::string newTokenSuffix = "/v";
        res = GetTokenSuffix(cache, attributes, oldTokenId.v, newTokenSuffix);
        if (!res) {
            LogPrintf("Token split failed on GetTokenSuffix %s\n", res.msg);
            continue;
        }

        CTokenImplementation newToken{*token};
        newToken.creationHeight = pindex->nHeight;
        assert(creationTxs.count(id));
        newToken.creationTx = creationTxs.at(id).first;
        newToken.minted = 0;

        token->symbol += newTokenSuffix;
        token->destructionHeight = pindex->nHeight;
        token->destructionTx = pindex->GetBlockHash();
        token->flags &=
            ~(static_cast<uint8_t>(CToken::TokenFlags::Default) | static_cast<uint8_t>(CToken::TokenFlags::LoanToken));
        token->flags |= static_cast<uint8_t>(CToken::TokenFlags::Finalized);

        res = view.SubMintedTokens(oldTokenId, token->minted);
        if (!res) {
            LogPrintf("Token split failed on SubMintedTokens %s\n", res.msg);
            continue;
        }

        UpdateTokenContext ctx{*token, blockCtx, true, true, false, pindex->GetBlockHash()};
        res = view.UpdateToken(ctx);
        if (!res) {
            LogPrintf("Token split failed on UpdateToken %s\n", res.msg);
            continue;
        }

        auto resVal = view.CreateToken(newToken, blockCtx);
        if (!resVal) {
            LogPrintf("Token split failed on CreateToken %s\n", resVal.msg);
            continue;
        }

        const DCT_ID newTokenId{resVal.val->v};
        LogPrintf("Token split info: (symbol: %s, id: %d -> %d)\n", newToken.symbol, oldTokenId.v, newTokenId.v);

        std::vector<CDataStructureV0> eraseKeys;
        for (const auto &[key, value] : attributes.GetAttributesMap()) {
            if (const auto v0Key = std::get_if<CDataStructureV0>(&key); v0Key->type == AttributeTypes::Token) {
                if (v0Key->typeId == oldTokenId.v && v0Key->keyId == oldTokenId.v) {
                    CDataStructureV0 newKey{AttributeTypes::Token, newTokenId.v, v0Key->key, newTokenId.v};
                    attributes.SetValue(newKey, value);
                    eraseKeys.push_back(*v0Key);
                } else if (v0Key->typeId == oldTokenId.v) {
                    CDataStructureV0 newKey{AttributeTypes::Token, newTokenId.v, v0Key->key, v0Key->keyId};
                    attributes.SetValue(newKey, value);
                    eraseKeys.push_back(*v0Key);
                } else if (v0Key->keyId == oldTokenId.v) {
                    CDataStructureV0 newKey{AttributeTypes::Token, v0Key->typeId, v0Key->key, newTokenId.v};
                    attributes.SetValue(newKey, value);
                    eraseKeys.push_back(*v0Key);
                }
            }
        }

        for (const auto &key : eraseKeys) {
            attributes.EraseKey(key);
        }

        CDataStructureV0 newAscendantKey{AttributeTypes::Token, newTokenId.v, TokenKeys::Ascendant};
        attributes.SetValue(newAscendantKey, AscendantValue{oldTokenId.v, "split"});

        CDataStructureV0 descendantKey{AttributeTypes::Token, oldTokenId.v, TokenKeys::Descendant};
        attributes.SetValue(descendantKey, DescendantValue{newTokenId.v, static_cast<int32_t>(pindex->nHeight)});

        MigrateV1Remnants(cache, attributes, EconomyKeys::DFIP2203Current, oldTokenId, newTokenId, multiplier);
        MigrateV1Remnants(cache, attributes, EconomyKeys::DFIP2203Burned, oldTokenId, newTokenId, multiplier);
        MigrateV1Remnants(cache, attributes, EconomyKeys::DFIP2203Minted, oldTokenId, newTokenId, multiplier);
        MigrateV1Remnants(
            cache, attributes, EconomyKeys::BatchRoundingExcess, oldTokenId, newTokenId, multiplier, ParamIDs::Auction);
        MigrateV1Remnants(cache,
                          attributes,
                          EconomyKeys::ConsolidatedInterest,
                          oldTokenId,
                          newTokenId,
                          multiplier,
                          ParamIDs::Auction);

        CAmount totalBalance{0};

        res = PoolSplits(view, totalBalance, attributes, oldTokenId, newTokenId, pindex, creationTxs, multiplier);
        if (!res) {
            LogPrintf("Pool splits failed %s\n", res.msg);
            continue;
        }

        std::map<CScript, std::pair<CTokenAmount, CTokenAmount>> balanceUpdates;

        view.ForEachBalance([&, multiplier = multiplier](const CScript &owner, const CTokenAmount &balance) {
            if (oldTokenId.v == balance.nTokenId.v) {
                const auto newBalance = CalculateNewAmount(multiplier, balance.nValue);
                balanceUpdates.emplace(owner,
                                       std::pair<CTokenAmount, CTokenAmount>{
                                           {newTokenId, newBalance},
                                           balance
                });
                totalBalance += newBalance;

                auto newBalanceStr = CTokenAmount{newTokenId, newBalance}.ToString();
                LogPrint(BCLog::TOKENSPLIT,
                         "TokenSplit: T (%s: %s => %s)\n",
                         ScriptToString(owner),
                         balance.ToString(),
                         newBalanceStr);
            }
            return true;
        });

        LogPrintf(
            "Token split info: rebalance " /* Continued */
            "(id: %d, symbol: %s, accounts: %d, val: %d)\n",
            id,
            newToken.symbol,
            balanceUpdates.size(),
            totalBalance);

        res = view.AddMintedTokens(newTokenId, totalBalance);
        if (!res) {
            LogPrintf("Token split failed on AddMintedTokens %s\n", res.msg);
            continue;
        }

        try {
            for (const auto &[owner, balances] : balanceUpdates) {
                CAccountsHistoryWriter subView(view,
                                               pindex->nHeight,
                                               GetNextAccPosition(),
                                               pindex->GetBlockHash(),
                                               uint8_t(CustomTxType::TokenSplit));

                res = subView.SubBalance(owner, balances.second);
                if (!res) {
                    throw std::runtime_error(res.msg);
                }
                subView.Flush();

                CAccountsHistoryWriter addView(view,
                                               pindex->nHeight,
                                               GetNextAccPosition(),
                                               pindex->GetBlockHash(),
                                               uint8_t(CustomTxType::TokenSplit));

                res = addView.AddBalance(owner, balances.first);
                if (!res) {
                    throw std::runtime_error(res.msg);
                }
                addView.Flush();
            }
        } catch (const std::runtime_error &e) {
            LogPrintf("Token split failed. %s\n", res.msg);
            continue;
        }

        res = VaultSplits(view, attributes, oldTokenId, newTokenId, pindex->nHeight, multiplier);
        if (!res) {
            LogPrintf("Token splits failed: %s\n", res.msg);
            continue;
        }

        UpdateOracleSplitKeys<OracleSplits>(oldTokenId.v, attributes);
        UpdateOracleSplitKeys<OracleSplits64>(oldTokenId.v, attributes);

        view.SetVariable(attributes);

        // Migrate stored unlock
        if (pindex->nHeight >= consensus.DF20GrandCentralHeight) {
            bool updateStoredVar{};
            auto storedGovVars = view.GetStoredVariablesRange(pindex->nHeight, std::numeric_limits<uint32_t>::max());
            for (const auto &[varHeight, var] : storedGovVars) {
                if (var->GetName() != "ATTRIBUTES") {
                    continue;
                }
                updateStoredVar = false;

                if (const auto attrVar = std::dynamic_pointer_cast<ATTRIBUTES>(var); attrVar) {
                    const auto attrMap = attrVar->GetAttributesMap();
                    std::vector<CDataStructureV0> keysToUpdate;
                    for (const auto &[key, value] : attrMap) {
                        if (const auto attrV0 = std::get_if<CDataStructureV0>(&key); attrV0) {
                            if (attrV0->type == AttributeTypes::Locks && attrV0->typeId == ParamIDs::TokenID &&
                                attrV0->key == oldTokenId.v) {
                                keysToUpdate.push_back(*attrV0);
                                updateStoredVar = true;
                            }
                        }
                    }
                    for (auto &key : keysToUpdate) {
                        const auto value = attrVar->GetValue(key, false);
                        attrVar->EraseKey(key);
                        key.key = newTokenId.v;
                        attrVar->SetValue(key, value);
                    }
                }

                if (updateStoredVar) {
                    view.SetStoredVariables({var}, varHeight);
                }
            }
        }

        if (pindex->nHeight >= consensus.DF23Height) {
            view.SetTokenSplitMultiplier(id, newTokenId.v, multiplier);
        }

        view.Flush();
        LogPrintf("Token split completed: (id: %d, mul: %d, time: %dms)\n", id, multiplier, GetTimeMillis() - time);
    }
}

static void ProcessTokenSplits(const CBlockIndex *pindex,
                               CCustomCSView &cache,
                               const CreationTxs &creationTxs,
                               BlockContext &blockCtx) {
    const auto &consensus = blockCtx.GetConsensus();
    if (pindex->nHeight < consensus.DF16FortCanningCrunchHeight) {
        return;
    }
    const auto attributes = cache.GetAttributes();

    CDataStructureV0 splitKey{AttributeTypes::Oracles, OracleIDs::Splits, static_cast<uint32_t>(pindex->nHeight)};

    if (const auto splits32 = attributes->GetValue(splitKey, OracleSplits{}); !splits32.empty()) {
        attributes->EraseKey(splitKey);
        cache.SetVariable(*attributes);
        ExecuteTokenSplits(pindex, cache, creationTxs, consensus, *attributes, splits32, blockCtx);
    } else if (const auto splits64 = attributes->GetValue(splitKey, OracleSplits64{}); !splits64.empty()) {
        attributes->EraseKey(splitKey);
        cache.SetVariable(*attributes);
        ExecuteTokenSplits(pindex, cache, creationTxs, consensus, *attributes, splits64, blockCtx);
    }
}

static void ProcessFuturesDUSD(const CBlockIndex *pindex, CCustomCSView &cache, const Consensus::Params &consensus) {
    if (pindex->nHeight < consensus.DF17FortCanningSpringHeight) {
        return;
    }

    auto attributes = cache.GetAttributes();

    CDataStructureV0 activeKey{AttributeTypes::Param, ParamIDs::DFIP2206F, DFIPKeys::Active};
    CDataStructureV0 blockKey{AttributeTypes::Param, ParamIDs::DFIP2206F, DFIPKeys::BlockPeriod};
    CDataStructureV0 rewardKey{AttributeTypes::Param, ParamIDs::DFIP2206F, DFIPKeys::RewardPct};
    if (!attributes->GetValue(activeKey, false) || !attributes->CheckKey(blockKey) ||
        !attributes->CheckKey(rewardKey)) {
        return;
    }

    CDataStructureV0 startKey{AttributeTypes::Param, ParamIDs::DFIP2206F, DFIPKeys::StartBlock};
    const auto startBlock = attributes->GetValue(startKey, CAmount{});
    if (pindex->nHeight < startBlock) {
        return;
    }

    const auto blockPeriod = attributes->GetValue(blockKey, CAmount{});
    if ((pindex->nHeight - startBlock) % blockPeriod != 0) {
        return;
    }

    auto time = GetTimeMillis();
    LogPrintf("Future swap DUSD settlement in progress.. (height: %d)\n", pindex->nHeight);

    const auto rewardPct = attributes->GetValue(rewardKey, CAmount{});
    const auto discount{COIN - rewardPct};

    const auto useNextPrice{false}, requireLivePrice{true};
    const auto discountPrice = cache.GetAmountInCurrency(discount, {"DFI", "USD"}, useNextPrice, requireLivePrice);

    CDataStructureV0 liveKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2206FCurrent};
    auto balances = attributes->GetValue(liveKey, CBalances{});

    const auto contractAddressValue = GetFutureSwapContractAddress(SMART_CONTRACT_DFIP2206F);
    assert(contractAddressValue);

    const auto dfiID{DCT_ID{}};

    if (!discountPrice) {
        std::vector<std::pair<CFuturesUserKey, CAmount>> refunds;

        cache.ForEachFuturesDUSD(
            [&](const CFuturesUserKey &key, const CAmount &amount) {
                refunds.emplace_back(key, amount);
                return true;
            },
            {static_cast<uint32_t>(pindex->nHeight), {}, std::numeric_limits<uint32_t>::max()});

        for (const auto &[key, amount] : refunds) {
            cache.EraseFuturesDUSD(key);

            const CTokenAmount source{dfiID, amount};

            CAccountsHistoryWriter subView(cache,
                                           pindex->nHeight,
                                           GetNextAccPosition(),
                                           pindex->GetBlockHash(),
                                           uint8_t(CustomTxType::FutureSwapRefund));
            subView.SubBalance(*contractAddressValue, source);
            subView.Flush();

            CAccountsHistoryWriter addView(cache,
                                           pindex->nHeight,
                                           GetNextAccPosition(),
                                           pindex->GetBlockHash(),
                                           uint8_t(CustomTxType::FutureSwapRefund));
            addView.AddBalance(key.owner, source);
            addView.Flush();

            LogPrint(
                BCLog::FUTURESWAP, "%s: Refund Owner %s value %s\n", __func__, key.owner.GetHex(), source.ToString());
            balances.Sub(source);
        }

        if (!refunds.empty()) {
            attributes->SetValue(liveKey, std::move(balances));
        }

        cache.SetVariable(*attributes);

        LogPrintf("Future swap DUSD refunded due to no live price: (%d refunds (height: %d, time: %dms)\n",
                  refunds.size(),
                  pindex->nHeight,
                  GetTimeMillis() - time);

        return;
    }

    CDataStructureV0 burnKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2206FBurned};
    CDataStructureV0 mintedKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::DFIP2206FMinted};

    auto burned = attributes->GetValue(burnKey, CBalances{});
    auto minted = attributes->GetValue(mintedKey, CBalances{});

    std::set<CFuturesUserKey> deletionPending;

    auto swapCounter{0};

    cache.ForEachFuturesDUSD(
        [&](const CFuturesUserKey &key, const CAmount &amount) {
            CAccountsHistoryWriter view(cache,
                                        pindex->nHeight,
                                        GetNextAccPosition(),
                                        pindex->GetBlockHash(),
                                        uint8_t(CustomTxType::FutureSwapExecution));

            deletionPending.insert(key);

            const auto tokenDUSD = view.GetToken("DUSD");
            assert(tokenDUSD);

            const auto total = MultiplyAmounts(amount, discountPrice);
            view.AddMintedTokens(tokenDUSD->first, total);
            CTokenAmount destination{tokenDUSD->first, total};
            view.AddBalance(key.owner, destination);
            burned.Add({dfiID, amount});
            minted.Add(destination);
            ++swapCounter;
            LogPrint(BCLog::FUTURESWAP,
                     "ProcessFuturesDUSD (): Payment Owner %s source %d destination %s\n",
                     key.owner.GetHex(),
                     amount,
                     destination.ToString());

            view.Flush();

            return true;
        },
        {static_cast<uint32_t>(pindex->nHeight), {}, std::numeric_limits<uint32_t>::max()});

    for (const auto &key : deletionPending) {
        cache.EraseFuturesDUSD(key);
    }

    attributes->SetValue(burnKey, std::move(burned));
    attributes->SetValue(mintedKey, std::move(minted));

    LogPrintf("Future swap DUSD settlement completed: (%d swaps (height: %d, time: %dms)\n",
              swapCounter,
              pindex->nHeight,
              GetTimeMillis() - time);

    cache.SetVariable(*attributes);
}

static void ProcessNegativeInterest(const CBlockIndex *pindex, CCustomCSView &cache) {
    if (!gArgs.GetBoolArg("-negativeinterest", DEFAULT_NEGATIVE_INTEREST)) {
        return;
    }

    auto attributes = cache.GetAttributes();

    DCT_ID dusd{};
    const auto token = cache.GetTokenGuessId("DUSD", dusd);
    if (!token) {
        return;
    }

    CDataStructureV0 negativeInterestKey{AttributeTypes::Live, ParamIDs::Economy, EconomyKeys::NegativeInt};
    auto negativeInterestBalances = attributes->GetValue(negativeInterestKey, CBalances{});
    negativeInterestKey.key = EconomyKeys::NegativeIntCurrent;

    cache.ForEachLoanTokenAmount([&](const CVaultId &vaultId, const CBalances &balances) {
        for (const auto &[tokenId, amount] : balances.balances) {
            if (tokenId == dusd) {
                const auto rate = cache.GetInterestRate(vaultId, tokenId, pindex->nHeight);
                if (!rate) {
                    continue;
                }

                const auto totalInterest = TotalInterest(*rate, pindex->nHeight);
                if (totalInterest < 0) {
                    negativeInterestBalances.Add(
                        {tokenId, amount > std::abs(totalInterest) ? std::abs(totalInterest) : amount});
                }
            }
        }
        return true;
    });

    if (!negativeInterestBalances.balances.empty()) {
        attributes->SetValue(negativeInterestKey, negativeInterestBalances);
        cache.SetVariable(*attributes);
    }
}

static void ProcessProposalEvents(const CBlockIndex *pindex, CCustomCSView &cache, const Consensus::Params &consensus) {
    if (pindex->nHeight < consensus.DF20GrandCentralHeight) {
        return;
    }

    CDataStructureV0 enabledKey{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::GovernanceEnabled};

    auto attributes = cache.GetAttributes();

    auto funds = cache.GetCommunityBalance(CommunityAccountType::CommunityDevFunds);
    if (!attributes->GetValue(enabledKey, false)) {
        if (funds > 0) {
            cache.SubCommunityBalance(CommunityAccountType::CommunityDevFunds, funds);
            cache.AddBalance(consensus.foundationShareScript, {DCT_ID{0}, funds});
        }

        return;
    }

    auto balance = cache.GetBalance(consensus.foundationShareScript, DCT_ID{0});
    if (balance.nValue > 0) {
        cache.SubBalance(consensus.foundationShareScript, balance);
        cache.AddCommunityBalance(CommunityAccountType::CommunityDevFunds, balance.nValue);
    }

    std::set<uint256> activeMasternodes;
    cache.ForEachCycleProposal(
        [&](const CProposalId &propId, const CProposalObject &prop) {
            if (prop.status != CProposalStatusType::Voting) {
                return true;
            }

            if (activeMasternodes.empty()) {
                cache.ForEachMasternode([&](uint256 const &mnId, CMasternode node) {
                    if (node.IsActive(pindex->nHeight, cache) && node.mintedBlocks) {
                        activeMasternodes.insert(mnId);
                    }
                    return true;
                });
                if (activeMasternodes.empty()) {
                    return false;
                }
            }

            uint32_t voteYes = 0, voteNeutral = 0;
            std::set<uint256> voters{};
            cache.ForEachProposalVote(
                [&](CProposalId const &pId, uint8_t cycle, uint256 const &mnId, CProposalVoteType vote) {
                    if (pId != propId || cycle != prop.cycle) {
                        return false;
                    }
                    if (activeMasternodes.count(mnId)) {
                        voters.insert(mnId);
                        if (vote == CProposalVoteType::VoteYes) {
                            ++voteYes;
                        } else if (vote == CProposalVoteType::VoteNeutral) {
                            ++voteNeutral;
                        }
                    }
                    return true;
                },
                CMnVotePerCycle{propId, prop.cycle});

            // Redistributes fee among voting masternodes
            CDataStructureV0 feeRedistributionKey{
                AttributeTypes::Governance, GovernanceIDs::Proposals, GovernanceKeys::FeeRedistribution};

            if (voters.size() > 0 && attributes->GetValue(feeRedistributionKey, false)) {
                // return half fee among voting masternodes, the rest is burned at creation
                auto feeBack = prop.fee - prop.feeBurnAmount;
                auto amountPerVoter = DivideAmounts(feeBack, voters.size() * COIN);
                for (const auto mnId : voters) {
                    auto const mn = cache.GetMasternode(mnId);
                    assert(mn);

                    CScript scriptPubKey;
                    if (mn->rewardAddressType != 0) {
                        scriptPubKey = GetScriptForDestination(FromOrDefaultKeyIDToDestination(
                            mn->rewardAddress, TxDestTypeToKeyType(mn->rewardAddressType), KeyType::MNRewardKeyType));
                    } else {
                        scriptPubKey = GetScriptForDestination(FromOrDefaultKeyIDToDestination(
                            mn->ownerAuthAddress, TxDestTypeToKeyType(mn->ownerType), KeyType::MNOwnerKeyType));
                    }

                    CAccountsHistoryWriter subView(cache,
                                                   pindex->nHeight,
                                                   GetNextAccPosition(),
                                                   pindex->GetBlockHash(),
                                                   uint8_t(CustomTxType::ProposalFeeRedistribution));

                    auto res = subView.AddBalance(scriptPubKey, {DCT_ID{0}, amountPerVoter});
                    if (!res) {
                        LogPrintf("Proposal fee redistribution failed: %s Address: %s Amount: %d\n",
                                  res.msg,
                                  scriptPubKey.GetHex(),
                                  amountPerVoter);
                    }

                    if (pindex->nHeight >= consensus.DF22MetachainHeight) {
                        subView.CalculateOwnerRewards(scriptPubKey, pindex->nHeight);
                    }

                    subView.Flush();
                }

                // Burn leftover sats.
                auto burnAmount = feeBack - MultiplyAmounts(amountPerVoter, voters.size() * COIN);
                if (burnAmount > 0) {
                    auto res = cache.AddBalance(Params().GetConsensus().burnAddress, {{0}, burnAmount});
                    if (!res) {
                        LogPrintf("Burn of proposal fee redistribution leftover failed. Amount: %d\n", burnAmount);
                    }
                }
            }

            if (lround(voters.size() * 10000.f / activeMasternodes.size()) <= prop.quorum) {
                cache.UpdateProposalStatus(propId, pindex->nHeight, CProposalStatusType::Rejected);
                return true;
            }

            if (pindex->nHeight < consensus.DF22MetachainHeight &&
                lround(voteYes * 10000.f / voters.size()) <= prop.approvalThreshold) {
                cache.UpdateProposalStatus(propId, pindex->nHeight, CProposalStatusType::Rejected);
                return true;
            } else if (pindex->nHeight >= consensus.DF22MetachainHeight) {
                auto onlyNeutral = voters.size() == voteNeutral;
                if (onlyNeutral ||
                    lround(voteYes * 10000.f / (voters.size() - voteNeutral)) <= prop.approvalThreshold) {
                    cache.UpdateProposalStatus(propId, pindex->nHeight, CProposalStatusType::Rejected);
                    return true;
                }
            }

            if (prop.nCycles == prop.cycle) {
                cache.UpdateProposalStatus(propId, pindex->nHeight, CProposalStatusType::Completed);
            } else {
                assert(prop.nCycles > prop.cycle);
                cache.UpdateProposalCycle(propId, prop.cycle + 1, pindex->nHeight, consensus);
            }

            CDataStructureV0 payoutKey{AttributeTypes::Param, ParamIDs::Feature, DFIPKeys::CFPPayout};

            if (prop.type == CProposalType::CommunityFundProposal && attributes->GetValue(payoutKey, false)) {
                auto res = cache.SubCommunityBalance(CommunityAccountType::CommunityDevFunds, prop.nAmount);
                if (res) {
                    cache.CalculateOwnerRewards(prop.address, pindex->nHeight);
                    cache.AddBalance(prop.address, {DCT_ID{0}, prop.nAmount});
                } else {
                    LogPrintf("Fails to subtract community developement funds: %s\n", res.msg);
                }
            }

            return true;
        },
        pindex->nHeight);
}

static void ProcessMasternodeUpdates(const CBlockIndex *pindex,
                                     CCustomCSView &cache,
                                     const CCoinsViewCache &view,
                                     const Consensus::Params &consensus) {
    if (pindex->nHeight < consensus.DF20GrandCentralHeight) {
        return;
    }
    // Apply any pending masternode owner changes
    cache.ForEachNewCollateral([&](const uint256 &key, const MNNewOwnerHeightValue &value) {
        if (value.blockHeight == static_cast<uint32_t>(pindex->nHeight)) {
            auto node = cache.GetMasternode(value.masternodeID);
            assert(node);
            assert(key == node->collateralTx);
            const auto &coin = view.AccessCoin({node->collateralTx, 1});
            assert(!coin.IsSpent());
            CTxDestination dest;
            assert(ExtractDestination(coin.out.scriptPubKey, dest));
            const CKeyID keyId = CKeyID::FromOrDefaultDestination(dest, KeyType::MNOwnerKeyType);
            cache.UpdateMasternodeOwner(value.masternodeID, *node, dest.index(), keyId);
        }
        return true;
    });

    std::set<CKeyID> pendingToErase;
    cache.ForEachPendingHeight([&](const CKeyID &ownerAuthAddress, const uint32_t &height) {
        if (height == static_cast<uint32_t>(pindex->nHeight)) {
            pendingToErase.insert(ownerAuthAddress);
        }
        return true;
    });

    for (const auto &keyID : pendingToErase) {
        cache.ErasePendingHeight(keyID);
    }
}

static void ProcessGrandCentralEvents(const CBlockIndex *pindex,
                                      CCustomCSView &cache,
                                      const Consensus::Params &consensus) {
    if (pindex->nHeight != consensus.DF20GrandCentralHeight) {
        return;
    }

    auto attributes = cache.GetAttributes();

    CDataStructureV0 key{AttributeTypes::Param, ParamIDs::Foundation, DFIPKeys::Members};
    attributes->SetValue(key, consensus.foundationMembers);
    cache.SetVariable(*attributes);
}

static void ProcessNullPoolSwapRefund(const CBlockIndex *pindex,
                                      CCustomCSView &cache,
                                      const Consensus::Params &consensus) {
    if (pindex->nHeight != consensus.DF23Height) {
        return;
    }

    const CScript nullSource{};
    for (const auto &[txid, height, address, amounts] : nullPoolSwapAmounts) {
        if (!cache.SubBalance(nullSource, amounts)) {
            continue;
        }
        const auto dest = DecodeDestination(address);
        if (!IsValidDestination(dest)) {
            continue;
        }
        const auto script = GetScriptForDestination(dest);
        if (!cache.AddBalance(script, amounts)) {
            continue;
        }
        LogPrintf("Null pool swap refund. Height: %d TX: %s Address: %s Amount: %s\n",
                  height,
                  txid.ToString(),
                  address,
                  amounts.ToString());
    }
}

static Res ValidateCoinbaseXVMOutput(const XVM &xvm, const FinalizeBlockCompletion &blockResult) {
    auto blockResultBlockHash = uint256::FromByteArray(blockResult.block_hash).GetHex();

    if (xvm.evm.blockHash != blockResultBlockHash) {
        return Res::Err("Incorrect EVM block hash in coinbase output");
    }

    if (xvm.evm.burntFee != blockResult.total_burnt_fees) {
        return Res::Err("Incorrect EVM burnt fee in coinbase output");
    }

    if (xvm.evm.priorityFee != blockResult.total_priority_fees) {
        return Res::Err("Incorrect EVM priority fee in coinbase output");
    }

    return Res::Ok();
}

static Res ProcessEVMQueue(const CBlock &block,
                           const CBlockIndex *pindex,
                           CCustomCSView &cache,
                           const CChainParams &chainparams,
                           BlockContext &blockCtx) {
    auto &evmTemplate = blockCtx.GetEVMTemplate();
    CKeyID minter;
    assert(block.ExtractMinterKey(minter));
    CScript minerAddress;

    if (!fMockNetwork) {
        const auto id = cache.GetMasternodeIdByOperator(minter);
        assert(id);
        const auto node = cache.GetMasternode(*id);
        assert(node);

        auto height = node->creationHeight;
        auto mnID = *id;
        if (!node->collateralTx.IsNull()) {
            const auto idHeight = cache.GetNewCollateral(node->collateralTx);
            assert(idHeight);
            height = idHeight->blockHeight - GetMnResignDelay(std::numeric_limits<int>::max());
            mnID = node->collateralTx;
        }

        const auto blockindex = ::ChainActive()[height];
        assert(blockindex);

        CTransactionRef tx;
        uint256 hash_block;
        assert(GetTransaction(mnID, tx, Params().GetConsensus(), hash_block, blockindex));
        assert(tx->vout.size() >= 2);

        CTxDestination dest;
        assert(ExtractDestination(tx->vout[1].scriptPubKey, dest));
        assert(dest.index() == PKHashType || dest.index() == WitV0KeyHashType);
        minerAddress = GetScriptForDestination(dest);
    } else {
        const auto dest = PKHash(minter);
        minerAddress = GetScriptForDestination(dest);
    }

    CrossBoundaryResult result;
    const auto blockResult = evm_try_unsafe_construct_block_in_template(result, evmTemplate->GetTemplate(), false);
    if (!result.ok) {
        return Res::Err(result.reason.c_str());
    }
    if (block.vtx[0]->vout.size() < 2) {
        return Res::Err("Not enough outputs in coinbase TX");
    }

    auto xvmRes = XVM::TryFrom(block.vtx[0]->vout[1].scriptPubKey);
    if (!xvmRes) {
        return std::move(xvmRes);
    }
    auto res = ValidateCoinbaseXVMOutput(*xvmRes, blockResult);
    if (!res) {
        return res;
    }

    auto evmBlockHash = uint256::FromByteArray(blockResult.block_hash).GetHex();
    res = cache.SetVMDomainBlockEdge(VMDomainEdge::DVMToEVM, block.GetHash().GetHex(), evmBlockHash);
    if (!res) {
        return res;
    }

    res = cache.SetVMDomainBlockEdge(VMDomainEdge::EVMToDVM, evmBlockHash, block.GetHash().GetHex());
    if (!res) {
        return res;
    }

    auto attributes = cache.GetAttributes();

    auto stats = attributes->GetValue(CEvmBlockStatsLive::Key, CEvmBlockStatsLive{});

    auto feeBurnt = static_cast<CAmount>(blockResult.total_burnt_fees);
    auto feePriority = static_cast<CAmount>(blockResult.total_priority_fees);
    stats.feeBurnt += feeBurnt;
    if (feeBurnt && stats.feeBurntMin > feeBurnt) {
        stats.feeBurntMin = feeBurnt;
        stats.feeBurntMinHash = block.GetHash();
    }
    if (stats.feeBurntMax < feeBurnt) {
        stats.feeBurntMax = feeBurnt;
        stats.feeBurntMaxHash = block.GetHash();
    }
    stats.feePriority += feePriority;
    if (feePriority && stats.feePriorityMin > feePriority) {
        stats.feePriorityMin = feePriority;
        stats.feePriorityMinHash = block.GetHash();
    }
    if (stats.feePriorityMax < feePriority) {
        stats.feePriorityMax = feePriority;
        stats.feePriorityMaxHash = block.GetHash();
    }

    auto transferDomainStats = attributes->GetValue(CTransferDomainStatsLive::Key, CTransferDomainStatsLive{});

    for (const auto &[id, amount] : transferDomainStats.dvmCurrent.balances) {
        if (id.v == 0) {
            if (amount + stats.feeBurnt + stats.feePriority > 0) {
                return Res::Err("More DFI moved from DVM to EVM than in. DVM Out: %s Fees: %s Total: %s\n",
                                GetDecimalString(amount),
                                GetDecimalString(stats.feeBurnt + stats.feePriority),
                                GetDecimalString(amount + stats.feeBurnt + stats.feePriority));
            }
        } else if (amount > 0) {
            return Res::Err(
                "More %s moved from DVM to EVM than in. DVM Out: %s\n", id.ToString(), GetDecimalString(amount));
        }
    }

    attributes->SetValue(CEvmBlockStatsLive::Key, stats);
    cache.SetVariable(*attributes);

    return Res::Ok();
}

static void FlushCacheCreateUndo(const CBlockIndex *pindex,
                                 CCustomCSView &mnview,
                                 CCustomCSView &cache,
                                 const uint256 hash) {
    // construct undo
    auto &flushable = cache.GetStorage();
    auto undo = CUndo::Construct(mnview.GetStorage(), flushable.GetRaw());
    // flush changes to underlying view
    cache.Flush();
    // write undo
    if (!undo.before.empty()) {
        mnview.SetUndo(UndoKey{static_cast<uint32_t>(pindex->nHeight), hash}, undo);
    }
}

Res ProcessDeFiEventFallible(const CBlock &block,
                             const CBlockIndex *pindex,
                             const CChainParams &chainparams,
                             const CreationTxs &creationTxs,
                             BlockContext &blockCtx) {
    auto isEvmEnabledForBlock = blockCtx.GetEVMEnabledForBlock();
    auto &mnview = blockCtx.GetView();
    CCustomCSView cache(mnview);

    // Loan splits
    ProcessTokenSplits(pindex, cache, creationTxs, blockCtx);

    if (isEvmEnabledForBlock) {
        // Process EVM block
        auto res = ProcessEVMQueue(block, pindex, cache, chainparams, blockCtx);
        if (!res) {
            return res;
        }
    }

    // Construct undo
    FlushCacheCreateUndo(pindex, mnview, cache, uint256S(std::string(64, '1')));

    return Res::Ok();
}

void ProcessDeFiEvent(const CBlock &block,
                      const CBlockIndex *pindex,
                      const CCoinsViewCache &view,
                      const CreationTxs &creationTxs,
                      BlockContext &blockCtx) {
    const auto &consensus = blockCtx.GetConsensus();
    auto &evmTemplate = blockCtx.GetEVMTemplate();
    auto &mnview = blockCtx.GetView();
    CCustomCSView cache(mnview);

    // calculate rewards to current block
    ProcessRewardEvents(pindex, cache, consensus);

    // close expired orders, refund all expired DFC HTLCs at this block height
    ProcessICXEvents(pindex, cache, consensus);

    // Remove `Finalized` and/or `LPS` flags _possibly_set_ by bytecoded (cheated) txs before bayfront fork
    if (pindex->nHeight == consensus.DF2BayfrontHeight - 1) {  // call at block _before_ fork
        cache.BayfrontFlagsCleanup();
    }

    // burn DFI on Eunos height
    ProcessEunosEvents(pindex, cache, consensus);

    // set oracle prices
    ProcessOracleEvents(pindex, cache, consensus);

    // loan scheme, collateral ratio, liquidations
    ProcessLoanEvents(pindex, cache, consensus);

    // Must be before set gov by height to clear futures in case there's a disabling of loan token in v3+
    ProcessFutures(pindex, cache, consensus);

    // update governance variables
    ProcessGovEvents(pindex, cache, consensus, evmTemplate);

    // Migrate loan and collateral tokens to Gov vars.
    ProcessTokenToGovVar(pindex, cache, consensus);

    // Set height for live dex data
    if (cache.GetDexStatsEnabled().value_or(false)) {
        cache.SetDexStatsLastHeight(pindex->nHeight);
    }

    // DFI-to-DUSD swaps
    ProcessFuturesDUSD(pindex, cache, consensus);

    // Tally negative interest across vaults
    ProcessNegativeInterest(pindex, cache);

    // proposal activations
    ProcessProposalEvents(pindex, cache, consensus);

    // Masternode updates
    ProcessMasternodeUpdates(pindex, cache, view, consensus);

    // Migrate foundation members to attributes
    ProcessGrandCentralEvents(pindex, cache, consensus);

    // Refund null pool swap amounts
    ProcessNullPoolSwapRefund(pindex, cache, consensus);

    // construct undo
    FlushCacheCreateUndo(pindex, mnview, cache, uint256());
}

bool ExecuteTokenMigrationEVM(std::size_t mnview_ptr, const TokenAmount oldAmount, TokenAmount &newAmount) {
    auto cache = reinterpret_cast<CCustomCSView *>(static_cast<uintptr_t>(mnview_ptr));
    CCustomCSView copy(*pcustomcsview);
    if (!cache) {
        // mnview_ptr will be 0 in case of a RPC `eth_call` or a debug_traceTransaction
        cache = &copy;
    }

    if (oldAmount.amount == 0) {
        return false;
    }

    const auto token = cache->GetToken(DCT_ID{oldAmount.id});
    if (!token) {
        return false;
    }

    const auto idMultiplierPair = cache->GetTokenSplitMultiplier(oldAmount.id);
    if (!idMultiplierPair) {
        newAmount = oldAmount;
        return true;
    }

    auto &[id, multiplierVariant] = *idMultiplierPair;

    newAmount.id = id;

    if (const auto multiplier64 = std::get_if<CAmount>(&multiplierVariant); multiplier64) {
        newAmount.amount = CalculateNewAmount(*multiplier64, oldAmount.amount);
    } else {
        const auto multiplier32 = std::get<int32_t>(multiplierVariant);
        newAmount.amount = CalculateNewAmount(multiplier32, oldAmount.amount);
    }

    // Only increment minted tokens if there is no additional split on new token.
    if (const auto additionalSplit = cache->GetTokenSplitMultiplier(newAmount.id); !additionalSplit) {
        if (const auto res = cache->AddMintedTokens({id}, newAmount.amount); !res) {
            return res;
        }
    }

    auto attributes = cache->GetAttributes();
    auto stats = attributes->GetValue(CTransferDomainStatsLive::Key, CTransferDomainStatsLive{});

    // Transfer out old token
    auto outAmount = CTokenAmount{{oldAmount.id}, static_cast<CAmount>(oldAmount.amount)};
    stats.evmOut.Add(outAmount);
    stats.evmCurrent.Sub(outAmount);
    stats.evmDvmTotal.Add(outAmount);
    stats.dvmIn.Add(outAmount);
    stats.dvmCurrent.Add(outAmount);

    // Transfer in new token
    auto inAmount = CTokenAmount{{newAmount.id}, static_cast<CAmount>(newAmount.amount)};
    stats.dvmEvmTotal.Add(inAmount);
    stats.dvmOut.Add(inAmount);
    stats.dvmCurrent.Sub(inAmount);
    stats.evmIn.Add(inAmount);
    stats.evmCurrent.Add(inAmount);

    attributes->SetValue(CTransferDomainStatsLive::Key, stats);
    if (const auto res = cache->SetVariable(*attributes); !res) {
        return res;
    }

    return true;
}

Res ExecuteTokenMigrationTransferDomain(CCustomCSView &view, CTokenAmount &amount) {
    if (amount.nValue == 0) {
        return Res::Ok();
    }

    while (true) {
        const auto idMultiplierPair = view.GetTokenSplitMultiplier(amount.nTokenId.v);
        if (!idMultiplierPair) {
            return Res::Ok();
        }

        if (const auto token = view.GetToken(amount.nTokenId); !token) {
            return Res::Err("Token not found");
        }

        auto &[id, multiplierVariant] = *idMultiplierPair;

        if (const auto multiplier64 = std::get_if<CAmount>(&multiplierVariant); multiplier64) {
            amount = {{id}, CalculateNewAmount(*multiplier64, amount.nValue)};
        } else {
            const auto multiplier32 = std::get<int32_t>(multiplierVariant);
            amount = {{id}, CalculateNewAmount(multiplier32, amount.nValue)};
        }

        if (const auto res = view.AddMintedTokens(amount.nTokenId, amount.nValue); !res) {
            return res;
        }
    }
}
