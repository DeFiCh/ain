// Copyright (c) 2023 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/consensus/icxorders.h>
#include <masternodes/masternodes.h>
#include <masternodes/mn_checks.h>

static CAmount GetDFIperBTC(const CPoolPair &BTCDFIPoolPair) {
    if (BTCDFIPoolPair.idTokenA == DCT_ID({0}))
        return DivideAmounts(BTCDFIPoolPair.reserveA, BTCDFIPoolPair.reserveB);
    return DivideAmounts(BTCDFIPoolPair.reserveB, BTCDFIPoolPair.reserveA);
}

CAmount CICXOrdersConsensus::CalculateTakerFee(CAmount amount) const {
    auto tokenBTC = mnview.GetToken(CICXOrder::TOKEN_BTC);
    assert(tokenBTC);
    auto pair = mnview.GetPoolPair(tokenBTC->first, DCT_ID{0});
    assert(pair);
    return (arith_uint256(amount) * mnview.ICXGetTakerFeePerBTC() / COIN * GetDFIperBTC(pair->second) / COIN)
            .GetLow64();
}

DCT_ID CICXOrdersConsensus::FindTokenByPartialSymbolName(const std::string &symbol) const {
    DCT_ID res{0};
    mnview.ForEachToken(
            [&](DCT_ID id, CTokenImplementation token) {
                if (token.symbol.find(symbol) == 0) {
                    res = id;
                    return false;
                }
                return true;
            },
            DCT_ID{1});
    assert(res.v != 0);
    return res;
}

Res CICXOrdersConsensus::operator()(const CICXCreateOrderMessage &obj) const {
    Require(CheckCustomTx());

    CICXOrderImplemetation order;
    static_cast<CICXOrder &>(order) = obj;

    order.creationTx     = tx.GetHash();
    order.creationHeight = height;

    Require(HasAuth(order.ownerAddress), "tx must have at least one input from order owner");

    Require(mnview.GetToken(order.idToken), "token %s does not exist!", order.idToken.ToString());

    if (order.orderType == CICXOrder::TYPE_INTERNAL) {
        Require(order.receivePubkey.IsFullyValid(), "receivePubkey must be valid pubkey");

        // subtract the balance from tokenFrom to dedicate them for the order
        CScript txidAddr(order.creationTx.begin(), order.creationTx.end());
        CalculateOwnerRewards(order.ownerAddress);
        Require(TransferTokenBalance(order.idToken, order.amountFrom, order.ownerAddress, txidAddr));
    }

    return mnview.ICXCreateOrder(order);
}

Res CICXOrdersConsensus::operator()(const CICXMakeOfferMessage &obj) const {
    Require(CheckCustomTx());

    CICXMakeOfferImplemetation makeoffer;
    static_cast<CICXMakeOffer &>(makeoffer) = obj;

    makeoffer.creationTx     = tx.GetHash();
    makeoffer.creationHeight = height;

    Require(HasAuth(makeoffer.ownerAddress), "tx must have at least one input from order owner");

    auto order = mnview.GetICXOrderByCreationTx(makeoffer.orderTx);
    Require(order, "order with creation tx " + makeoffer.orderTx.GetHex() + " does not exists!");

    auto expiry = static_cast<int>(height) < consensus.EunosPayaHeight ? CICXMakeOffer::DEFAULT_EXPIRY
                                                                       : CICXMakeOffer::EUNOSPAYA_DEFAULT_EXPIRY;

    Require(makeoffer.expiry >= expiry, "offer expiry must be greater than %d!", expiry - 1);

    CScript txidAddr(makeoffer.creationTx.begin(), makeoffer.creationTx.end());

    if (order->orderType == CICXOrder::TYPE_INTERNAL) {
        // calculating takerFee
        makeoffer.takerFee = CalculateTakerFee(makeoffer.amount);
    } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
        Require(makeoffer.receivePubkey.IsFullyValid(), "receivePubkey must be valid pubkey");

        // calculating takerFee
        CAmount BTCAmount(static_cast<CAmount>(
                                  (arith_uint256(makeoffer.amount) * arith_uint256(COIN) / arith_uint256(order->orderPrice)).GetLow64()));
        makeoffer.takerFee = CalculateTakerFee(BTCAmount);
    }

    // locking takerFee in offer txidaddr
    CalculateOwnerRewards(makeoffer.ownerAddress);
    Require(TransferTokenBalance(DCT_ID{0}, makeoffer.takerFee, makeoffer.ownerAddress, txidAddr));

    return mnview.ICXMakeOffer(makeoffer);
}

Res CICXOrdersConsensus::operator()(const CICXSubmitDFCHTLCMessage &obj) const {
    Require(CheckCustomTx());

    CICXSubmitDFCHTLCImplemetation submitdfchtlc;
    static_cast<CICXSubmitDFCHTLC &>(submitdfchtlc) = obj;

    submitdfchtlc.creationTx     = tx.GetHash();
    submitdfchtlc.creationHeight = height;

    auto offer = mnview.GetICXMakeOfferByCreationTx(submitdfchtlc.offerTx);
    Require(offer, "offer with creation tx %s does not exists!", submitdfchtlc.offerTx.GetHex());

    auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
    Require(order, "order with creation tx %s does not exists!", offer->orderTx.GetHex());

    Require(order->creationHeight + order->expiry >= height + submitdfchtlc.timeout,
            "order will expire before dfc htlc expires!");
    Require(!mnview.HasICXSubmitDFCHTLCOpen(submitdfchtlc.offerTx), "dfc htlc already submitted!");

    CScript srcAddr;
    if (order->orderType == CICXOrder::TYPE_INTERNAL) {
        // check auth
        Require(HasAuth(order->ownerAddress), "tx must have at least one input from order owner");
        Require(mnview.HasICXMakeOfferOpen(offer->orderTx, submitdfchtlc.offerTx),
                "offerTx (%s) has expired",
                submitdfchtlc.offerTx.GetHex());

        uint32_t timeout;
        if (static_cast<int>(height) < consensus.EunosPayaHeight)
            timeout = CICXSubmitDFCHTLC::MINIMUM_TIMEOUT;
        else
            timeout = CICXSubmitDFCHTLC::EUNOSPAYA_MINIMUM_TIMEOUT;

        Require(submitdfchtlc.timeout >= timeout, "timeout must be greater than %d", timeout - 1);

        srcAddr = CScript(order->creationTx.begin(), order->creationTx.end());

        CScript offerTxidAddr(offer->creationTx.begin(), offer->creationTx.end());

        auto calcAmount = MultiplyAmounts(submitdfchtlc.amount, order->orderPrice);
        Require(calcAmount <= offer->amount, "amount must be lower or equal the offer one");

        CAmount takerFee = offer->takerFee;
        // EunosPaya: calculating adjusted takerFee only if amount in htlc different than in offer
        if (static_cast<int>(height) >= consensus.EunosPayaHeight) {
            if (calcAmount < offer->amount) {
                auto BTCAmount = MultiplyAmounts(submitdfchtlc.amount, order->orderPrice);
                takerFee       = (arith_uint256(BTCAmount) * offer->takerFee / offer->amount).GetLow64();
            }
        } else {
            auto BTCAmount = MultiplyAmounts(submitdfchtlc.amount, order->orderPrice);
            takerFee       = CalculateTakerFee(BTCAmount);
        }

        // refund the rest of locked takerFee if there is difference
        if (offer->takerFee - takerFee) {
            CalculateOwnerRewards(offer->ownerAddress);
            Require(
                    TransferTokenBalance(DCT_ID{0}, offer->takerFee - takerFee, offerTxidAddr, offer->ownerAddress));

            // update the offer with adjusted takerFee
            offer->takerFee = takerFee;
            mnview.ICXUpdateMakeOffer(*offer);
        }

        // burn takerFee
        Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee, offerTxidAddr, consensus.burnAddress));

        // burn makerDeposit
        CalculateOwnerRewards(order->ownerAddress);
        Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee, order->ownerAddress, consensus.burnAddress));

    } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
        // check auth
        Require(HasAuth(offer->ownerAddress), "tx must have at least one input from offer owner");

        srcAddr = offer->ownerAddress;
        CalculateOwnerRewards(offer->ownerAddress);

        auto exthtlc = mnview.HasICXSubmitEXTHTLCOpen(submitdfchtlc.offerTx);
        Require(exthtlc,
                "offer (%s) needs to have ext htlc submitted first, but no external htlc found!",
                submitdfchtlc.offerTx.GetHex());

        auto calcAmount = MultiplyAmounts(exthtlc->amount, order->orderPrice);
        Require(submitdfchtlc.amount == calcAmount, "amount must be equal to calculated exthtlc amount");

        Require(submitdfchtlc.hash == exthtlc->hash,
                "Invalid hash, dfc htlc hash is different than extarnal htlc hash - %s != %s",
                submitdfchtlc.hash.GetHex(),
                exthtlc->hash.GetHex());

        uint32_t timeout, btcBlocksInDfi;
        if (static_cast<int>(height) < consensus.EunosPayaHeight) {
            timeout        = CICXSubmitDFCHTLC::MINIMUM_2ND_TIMEOUT;
            btcBlocksInDfi = CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS;
        } else {
            timeout        = CICXSubmitDFCHTLC::EUNOSPAYA_MINIMUM_2ND_TIMEOUT;
            btcBlocksInDfi = CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS;
        }

        Require(submitdfchtlc.timeout >= timeout, "timeout must be greater than %d", timeout - 1);
        Require(submitdfchtlc.timeout < (exthtlc->creationHeight + (exthtlc->timeout * btcBlocksInDfi)) - height,
                "timeout must be less than expiration period of 1st htlc in DFI blocks");
    }

    // subtract the balance from order txidaddr or offer owner address and dedicate them for the dfc htlc
    CScript htlcTxidAddr(submitdfchtlc.creationTx.begin(), submitdfchtlc.creationTx.end());

    Require(TransferTokenBalance(order->idToken, submitdfchtlc.amount, srcAddr, htlcTxidAddr));
    return mnview.ICXSubmitDFCHTLC(submitdfchtlc);
}

Res CICXOrdersConsensus::operator()(const CICXSubmitEXTHTLCMessage &obj) const {
    Require(CheckCustomTx());

    CICXSubmitEXTHTLCImplemetation submitexthtlc;
    static_cast<CICXSubmitEXTHTLC &>(submitexthtlc) = obj;

    submitexthtlc.creationTx     = tx.GetHash();
    submitexthtlc.creationHeight = height;

    auto offer = mnview.GetICXMakeOfferByCreationTx(submitexthtlc.offerTx);
    Require(offer, "order with creation tx %s does not exists!", submitexthtlc.offerTx.GetHex());

    auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
    Require(order, "order with creation tx %s does not exists!", offer->orderTx.GetHex());

    Require(order->creationHeight + order->expiry >=
            height + (submitexthtlc.timeout * CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS),
            "order will expire before ext htlc expires!");

    Require(!mnview.HasICXSubmitEXTHTLCOpen(submitexthtlc.offerTx), "ext htlc already submitted!");

    if (order->orderType == CICXOrder::TYPE_INTERNAL) {
        Require(HasAuth(offer->ownerAddress), "tx must have at least one input from offer owner");

        auto dfchtlc = mnview.HasICXSubmitDFCHTLCOpen(submitexthtlc.offerTx);
        Require(dfchtlc,
                "offer (%s) needs to have dfc htlc submitted first, but no dfc htlc found!",
                submitexthtlc.offerTx.GetHex());

        auto calcAmount = MultiplyAmounts(dfchtlc->amount, order->orderPrice);
        Require(submitexthtlc.amount == calcAmount, "amount must be equal to calculated dfchtlc amount");
        Require(submitexthtlc.hash == dfchtlc->hash,
                "Invalid hash, external htlc hash is different than dfc htlc hash");

        uint32_t timeout, btcBlocksInDfi;
        if (static_cast<int>(height) < consensus.EunosPayaHeight) {
            timeout        = CICXSubmitEXTHTLC::MINIMUM_2ND_TIMEOUT;
            btcBlocksInDfi = CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS;
        } else {
            timeout        = CICXSubmitEXTHTLC::EUNOSPAYA_MINIMUM_2ND_TIMEOUT;
            btcBlocksInDfi = CICXSubmitEXTHTLC::EUNOSPAYA_BTC_BLOCKS_IN_DFI_BLOCKS;
        }

        Require(submitexthtlc.timeout >= timeout, "timeout must be greater than %d", timeout - 1);
        Require(submitexthtlc.timeout * btcBlocksInDfi < (dfchtlc->creationHeight + dfchtlc->timeout) - height,
                "timeout must be less than expiration period of 1st htlc in DFC blocks");

    } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
        Require(HasAuth(order->ownerAddress), "tx must have at least one input from order owner");
        Require(mnview.HasICXMakeOfferOpen(offer->orderTx, submitexthtlc.offerTx),
                "offerTx (%s) has expired",
                submitexthtlc.offerTx.GetHex());

        uint32_t timeout;
        if (static_cast<int>(height) < consensus.EunosPayaHeight)
            timeout = CICXSubmitEXTHTLC::MINIMUM_TIMEOUT;
        else
            timeout = CICXSubmitEXTHTLC::EUNOSPAYA_MINIMUM_TIMEOUT;

        Require(submitexthtlc.timeout >= timeout, "timeout must be greater than %d", timeout - 1);

        CScript offerTxidAddr(offer->creationTx.begin(), offer->creationTx.end());

        auto calcAmount = MultiplyAmounts(submitexthtlc.amount, order->orderPrice);
        Require(calcAmount <= offer->amount, "amount must be lower or equal the offer one");

        CAmount takerFee = offer->takerFee;
        // EunosPaya: calculating adjusted takerFee only if amount in htlc different than in offer
        if (static_cast<int>(height) >= consensus.EunosPayaHeight) {
            if (calcAmount < offer->amount) {
                auto BTCAmount = DivideAmounts(offer->amount, order->orderPrice);
                takerFee       = (arith_uint256(submitexthtlc.amount) * offer->takerFee / BTCAmount).GetLow64();
            }
        } else
            takerFee = CalculateTakerFee(submitexthtlc.amount);

        // refund the rest of locked takerFee if there is difference
        if (offer->takerFee - takerFee) {
            CalculateOwnerRewards(offer->ownerAddress);
            Require(
                    TransferTokenBalance(DCT_ID{0}, offer->takerFee - takerFee, offerTxidAddr, offer->ownerAddress));

            // update the offer with adjusted takerFee
            offer->takerFee = takerFee;
            mnview.ICXUpdateMakeOffer(*offer);
        }

        // burn takerFee
        Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee, offerTxidAddr, consensus.burnAddress));

        // burn makerDeposit
        CalculateOwnerRewards(order->ownerAddress);
        Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee, order->ownerAddress, consensus.burnAddress));
    }

    return mnview.ICXSubmitEXTHTLC(submitexthtlc);
}

Res CICXOrdersConsensus::operator()(const CICXClaimDFCHTLCMessage &obj) const {
    Require(CheckCustomTx());

    CICXClaimDFCHTLCImplemetation claimdfchtlc;
    static_cast<CICXClaimDFCHTLC &>(claimdfchtlc) = obj;

    claimdfchtlc.creationTx     = tx.GetHash();
    claimdfchtlc.creationHeight = height;

    auto dfchtlc = mnview.GetICXSubmitDFCHTLCByCreationTx(claimdfchtlc.dfchtlcTx);
    Require(dfchtlc, "dfc htlc with creation tx %s does not exists!", claimdfchtlc.dfchtlcTx.GetHex());

    Require(mnview.HasICXSubmitDFCHTLCOpen(dfchtlc->offerTx), "dfc htlc not found or already claimed or refunded!");

    uint256 calcHash;
    uint8_t calcSeedBytes[32];
    CSHA256().Write(claimdfchtlc.seed.data(), claimdfchtlc.seed.size()).Finalize(calcSeedBytes);
    calcHash.SetHex(HexStr(calcSeedBytes, calcSeedBytes + 32));

    Require(dfchtlc->hash == calcHash,
            "hash generated from given seed is different than in dfc htlc: %s - %s!",
            calcHash.GetHex(),
            dfchtlc->hash.GetHex());

    auto offer = mnview.GetICXMakeOfferByCreationTx(dfchtlc->offerTx);
    Require(offer, "offer with creation tx %s does not exists!", dfchtlc->offerTx.GetHex());

    auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
    Require(order, "order with creation tx %s does not exists!", offer->orderTx.GetHex());

    auto exthtlc = mnview.HasICXSubmitEXTHTLCOpen(dfchtlc->offerTx);
    if (static_cast<int>(height) < consensus.EunosPayaHeight)
        Require(exthtlc, "cannot claim, external htlc for this offer does not exists or expired!");

    // claim DFC HTLC to receiveAddress
    CalculateOwnerRewards(order->ownerAddress);
    CScript htlcTxidAddr(dfchtlc->creationTx.begin(), dfchtlc->creationTx.end());

    if (order->orderType == CICXOrder::TYPE_INTERNAL)
        Require(TransferTokenBalance(order->idToken, dfchtlc->amount, htlcTxidAddr, offer->ownerAddress));
    else if (order->orderType == CICXOrder::TYPE_EXTERNAL)
        Require(TransferTokenBalance(order->idToken, dfchtlc->amount, htlcTxidAddr, order->ownerAddress));

    // refund makerDeposit
    Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee, CScript(), order->ownerAddress));

    // makerIncentive
    Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee * 25 / 100, CScript(), order->ownerAddress));

    // maker bonus only on fair dBTC/BTC (1:1) trades for now
    DCT_ID BTC = FindTokenByPartialSymbolName(CICXOrder::TOKEN_BTC);
    if (order->idToken == BTC && order->orderPrice == COIN) {
        if ((IsTestNetwork() && height >= 1250000) ||
            Params().NetworkIDString() == CBaseChainParams::REGTEST) {
            Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee * 50 / 100, CScript(), order->ownerAddress));
        } else {
            Require(TransferTokenBalance(BTC, offer->takerFee * 50 / 100, CScript(), order->ownerAddress));
        }
    }

    if (order->orderType == CICXOrder::TYPE_INTERNAL)
        order->amountToFill -= dfchtlc->amount;
    else if (order->orderType == CICXOrder::TYPE_EXTERNAL)
        order->amountToFill -= DivideAmounts(dfchtlc->amount, order->orderPrice);

    // Order fulfilled, close order.
    if (order->amountToFill == 0) {
        order->closeTx     = claimdfchtlc.creationTx;
        order->closeHeight = height;
        Require(mnview.ICXCloseOrderTx(*order, CICXOrder::STATUS_FILLED));
    }

    Require(mnview.ICXClaimDFCHTLC(claimdfchtlc, offer->creationTx, *order));
    // Close offer
    Require(mnview.ICXCloseMakeOfferTx(*offer, CICXMakeOffer::STATUS_CLOSED));

    Require(mnview.ICXCloseDFCHTLC(*dfchtlc, CICXSubmitDFCHTLC::STATUS_CLAIMED));

    if (static_cast<int>(height) >= consensus.EunosPayaHeight) {
        if (exthtlc)
            return mnview.ICXCloseEXTHTLC(*exthtlc, CICXSubmitEXTHTLC::STATUS_CLOSED);
        else
            return (Res::Ok());
    } else
        return mnview.ICXCloseEXTHTLC(*exthtlc, CICXSubmitEXTHTLC::STATUS_CLOSED);
}

Res CICXOrdersConsensus::operator()(const CICXCloseOrderMessage &obj) const {
    Require(CheckCustomTx());

    CICXCloseOrderImplemetation closeorder;
    static_cast<CICXCloseOrder &>(closeorder) = obj;

    closeorder.creationTx     = tx.GetHash();
    closeorder.creationHeight = height;

    auto order = mnview.GetICXOrderByCreationTx(closeorder.orderTx);
    Require(order, "order with creation tx %s does not exists!", closeorder.orderTx.GetHex());

    Require(order->closeTx.IsNull(), "order with creation tx %s is already closed!", closeorder.orderTx.GetHex());
    Require(mnview.HasICXOrderOpen(order->idToken, order->creationTx),
            "order with creation tx %s is already closed!",
            closeorder.orderTx.GetHex());

    // check auth
    Require(HasAuth(order->ownerAddress), "tx must have at least one input from order owner");

    order->closeTx     = closeorder.creationTx;
    order->closeHeight = closeorder.creationHeight;

    if (order->orderType == CICXOrder::TYPE_INTERNAL && order->amountToFill > 0) {
        // subtract the balance from txidAddr and return to owner
        CScript txidAddr(order->creationTx.begin(), order->creationTx.end());
        CalculateOwnerRewards(order->ownerAddress);
        Require(TransferTokenBalance(order->idToken, order->amountToFill, txidAddr, order->ownerAddress));
    }

    Require(mnview.ICXCloseOrder(closeorder));
    return mnview.ICXCloseOrderTx(*order, CICXOrder::STATUS_CLOSED);
}

Res CICXOrdersConsensus::operator()(const CICXCloseOfferMessage &obj) const {
    Require(CheckCustomTx());

    CICXCloseOfferImplemetation closeoffer;
    static_cast<CICXCloseOffer &>(closeoffer) = obj;

    closeoffer.creationTx     = tx.GetHash();
    closeoffer.creationHeight = height;

    auto offer = mnview.GetICXMakeOfferByCreationTx(closeoffer.offerTx);
    Require(offer, "offer with creation tx %s does not exists!", closeoffer.offerTx.GetHex());

    Require(offer->closeTx.IsNull(), "offer with creation tx %s is already closed!", closeoffer.offerTx.GetHex());
    Require(mnview.HasICXMakeOfferOpen(offer->orderTx, offer->creationTx),
            "offer with creation tx %s does not exists!",
            closeoffer.offerTx.GetHex());

    auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
    Require(order, "order with creation tx %s does not exists!", offer->orderTx.GetHex());

    // check auth
    Require(HasAuth(offer->ownerAddress), "tx must have at least one input from offer owner");

    offer->closeTx     = closeoffer.creationTx;
    offer->closeHeight = closeoffer.creationHeight;

    bool isPreEunosPaya = static_cast<int>(height) < consensus.EunosPayaHeight;

    if (order->orderType == CICXOrder::TYPE_INTERNAL &&
        !mnview.ExistedICXSubmitDFCHTLC(offer->creationTx, isPreEunosPaya)) {
        // subtract takerFee from txidAddr and return to owner
        CScript txidAddr(offer->creationTx.begin(), offer->creationTx.end());
        CalculateOwnerRewards(offer->ownerAddress);
        Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee, txidAddr, offer->ownerAddress));
    } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
        // subtract the balance from txidAddr and return to owner
        CScript txidAddr(offer->creationTx.begin(), offer->creationTx.end());
        CalculateOwnerRewards(offer->ownerAddress);
        if (isPreEunosPaya)
            Require(TransferTokenBalance(order->idToken, offer->amount, txidAddr, offer->ownerAddress));

        if (!mnview.ExistedICXSubmitEXTHTLC(offer->creationTx, isPreEunosPaya))
            Require(TransferTokenBalance(DCT_ID{0}, offer->takerFee, txidAddr, offer->ownerAddress));
    }

    Require(mnview.ICXCloseOffer(closeoffer));
    return mnview.ICXCloseMakeOfferTx(*offer, CICXMakeOffer::STATUS_CLOSED);
}
