// Copyright (c) 2021 The DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/consensus/icxorders.h>
#include <masternodes/masternodes.h>
#include <masternodes/icxorder.h>
#include <masternodes/tokens.h>
#include <primitives/transaction.h>

Res CICXOrdersConsensus::operator()(const CICXCreateOrderMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    CICXOrderImplemetation order;
    static_cast<CICXOrder&>(order) = obj;

    order.creationTx = tx.GetHash();
    order.creationHeight = height;

    if (!HasAuth(order.ownerAddress))
        return Res::Err("tx must have at least one input from order owner");

    if (!mnview.GetToken(order.idToken))
        return Res::Err("token %s does not exist!", order.idToken.ToString());

    if (order.orderType == CICXOrder::TYPE_INTERNAL) {
        if (!order.receivePubkey.IsFullyValid())
            return Res::Err("receivePubkey must be valid pubkey");

        // subtract the balance from tokenFrom to dedicate them for the order
        CScript txidAddr(order.creationTx.begin(), order.creationTx.end());
        res = TransferTokenBalance(order.idToken, order.amountFrom, order.ownerAddress, txidAddr);
    }

    return !res ? res : mnview.ICXCreateOrder(order);
}

Res CICXOrdersConsensus::operator()(const CICXMakeOfferMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    CICXMakeOfferImplemetation makeoffer;
    static_cast<CICXMakeOffer&>(makeoffer) = obj;

    makeoffer.creationTx = tx.GetHash();
    makeoffer.creationHeight = height;

    if (!HasAuth(makeoffer.ownerAddress))
        return Res::Err("tx must have at least one input from order owner");

    auto order = mnview.GetICXOrderByCreationTx(makeoffer.orderTx);
    if (!order)
        return Res::Err("order with creation tx " + makeoffer.orderTx.GetHex() + " does not exists!");

    auto expiry = static_cast<int>(height) < consensus.EunosPayaHeight ? CICXMakeOffer::DEFAULT_EXPIRY : CICXMakeOffer::EUNOSPAYA_DEFAULT_EXPIRY;

    if (makeoffer.expiry < expiry)
        return Res::Err("offer expiry must be greater than %d!", expiry - 1);

    CScript txidAddr(makeoffer.creationTx.begin(), makeoffer.creationTx.end());

    if (order->orderType == CICXOrder::TYPE_INTERNAL) {
        // calculating takerFee
        makeoffer.takerFee = CalculateTakerFee(makeoffer.amount);
    } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
        if (!makeoffer.receivePubkey.IsFullyValid())
            return Res::Err("receivePubkey must be valid pubkey");

        // calculating takerFee
        auto BTCAmount = DivideAmounts(makeoffer.amount, order->orderPrice);
        makeoffer.takerFee = CalculateTakerFee(BTCAmount);
    }

    // locking takerFee in offer txidaddr
    res = TransferTokenBalance(DCT_ID{0}, makeoffer.takerFee, makeoffer.ownerAddress, txidAddr);
    return !res ? res : mnview.ICXMakeOffer(makeoffer);
}

Res CICXOrdersConsensus::operator()(const CICXSubmitDFCHTLCMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    CICXSubmitDFCHTLCImplemetation submitdfchtlc;
    static_cast<CICXSubmitDFCHTLC&>(submitdfchtlc) = obj;

    submitdfchtlc.creationTx = tx.GetHash();
    submitdfchtlc.creationHeight = height;

    auto offer = mnview.GetICXMakeOfferByCreationTx(submitdfchtlc.offerTx);
    if (!offer)
        return Res::Err("offer with creation tx %s does not exists!", submitdfchtlc.offerTx.GetHex());

    auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
    if (!order)
        return Res::Err("order with creation tx %s does not exists!", offer->orderTx.GetHex());

    if (order->creationHeight + order->expiry < height + submitdfchtlc.timeout)
        return Res::Err("order will expire before dfc htlc expires!");

    if (mnview.HasICXSubmitDFCHTLCOpen(submitdfchtlc.offerTx))
        return Res::Err("dfc htlc already submitted!");

    CScript srcAddr;
    if (order->orderType == CICXOrder::TYPE_INTERNAL) {

        // check auth
        if (!HasAuth(order->ownerAddress))
            return Res::Err("tx must have at least one input from order owner");

        if (!mnview.HasICXMakeOfferOpen(offer->orderTx, submitdfchtlc.offerTx))
            return Res::Err("offerTx (%s) has expired", submitdfchtlc.offerTx.GetHex());

        uint32_t timeout;
        if (static_cast<int>(height) < consensus.EunosPayaHeight)
            timeout = CICXSubmitDFCHTLC::MINIMUM_TIMEOUT;
        else
            timeout = CICXSubmitDFCHTLC::EUNOSPAYA_MINIMUM_TIMEOUT;

        if (submitdfchtlc.timeout < timeout)
            return Res::Err("timeout must be greater than %d", timeout - 1);

        srcAddr = CScript(order->creationTx.begin(), order->creationTx.end());

        CScript offerTxidAddr(offer->creationTx.begin(), offer->creationTx.end());

        auto calcAmount = MultiplyAmounts(submitdfchtlc.amount, order->orderPrice);
        if (calcAmount > offer->amount)
            return Res::Err("amount must be lower or equal the offer one");

        CAmount takerFee = offer->takerFee;
        //EunosPaya: calculating adjusted takerFee only if amount in htlc different than in offer
        if (static_cast<int>(height) >= consensus.EunosPayaHeight) {
            if (calcAmount < offer->amount) {
                auto BTCAmount = MultiplyAmounts(submitdfchtlc.amount, order->orderPrice);
                takerFee = (arith_uint256(BTCAmount) * offer->takerFee / offer->amount).GetLow64();
            }
        } else {
            auto BTCAmount = MultiplyAmounts(submitdfchtlc.amount, order->orderPrice);
            takerFee = CalculateTakerFee(BTCAmount);
        }

        // refund the rest of locked takerFee if there is difference
        if (offer->takerFee - takerFee) {
            res = TransferTokenBalance(DCT_ID{0}, offer->takerFee - takerFee, offerTxidAddr, offer->ownerAddress);
            if (!res)
                return res;

            // update the offer with adjusted takerFee
            offer->takerFee = takerFee;
            mnview.ICXUpdateMakeOffer(*offer);
        }

        // burn takerFee
        res = TransferTokenBalance(DCT_ID{0}, offer->takerFee, offerTxidAddr, consensus.burnAddress);
        if (!res)
            return res;

        // burn makerDeposit
        res = TransferTokenBalance(DCT_ID{0}, offer->takerFee, order->ownerAddress, consensus.burnAddress);
        if (!res)
            return res;

    } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
        // check auth
        if (!HasAuth(offer->ownerAddress))
            return Res::Err("tx must have at least one input from offer owner");

        srcAddr = offer->ownerAddress;

        auto exthtlc = mnview.HasICXSubmitEXTHTLCOpen(submitdfchtlc.offerTx);
        if (!exthtlc)
            return Res::Err("offer (%s) needs to have ext htlc submitted first, but no external htlc found!", submitdfchtlc.offerTx.GetHex());

        auto calcAmount = MultiplyAmounts(exthtlc->amount, order->orderPrice);
        if (submitdfchtlc.amount != calcAmount)
            return Res::Err("amount must be equal to calculated exthtlc amount");

        if (submitdfchtlc.hash != exthtlc->hash)
            return Res::Err("Invalid hash, dfc htlc hash is different than extarnal htlc hash - %s != %s",
                            submitdfchtlc.hash.GetHex(),exthtlc->hash.GetHex());

        uint32_t timeout, btcBlocksInDfi;
        if (static_cast<int>(height) < consensus.EunosPayaHeight) {
            timeout = CICXSubmitDFCHTLC::MINIMUM_2ND_TIMEOUT;
            btcBlocksInDfi = CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS;
        } else {
            timeout = CICXSubmitDFCHTLC::EUNOSPAYA_MINIMUM_2ND_TIMEOUT;
            btcBlocksInDfi = CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS;
        }

        if (submitdfchtlc.timeout < timeout)
            return Res::Err("timeout must be greater than %d", timeout - 1);

        if (submitdfchtlc.timeout >= (exthtlc->creationHeight + (exthtlc->timeout * btcBlocksInDfi)) - height)
            return Res::Err("timeout must be less than expiration period of 1st htlc in DFI blocks");
    }

    // subtract the balance from order txidaddr or offer owner address and dedicate them for the dfc htlc
    CScript htlcTxidAddr(submitdfchtlc.creationTx.begin(), submitdfchtlc.creationTx.end());

    res = TransferTokenBalance(order->idToken, submitdfchtlc.amount, srcAddr, htlcTxidAddr);
    return !res ? res : mnview.ICXSubmitDFCHTLC(submitdfchtlc);
}

Res CICXOrdersConsensus::operator()(const CICXSubmitEXTHTLCMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    CICXSubmitEXTHTLCImplemetation submitexthtlc;
    static_cast<CICXSubmitEXTHTLC&>(submitexthtlc) = obj;

    submitexthtlc.creationTx = tx.GetHash();
    submitexthtlc.creationHeight = height;

    auto offer = mnview.GetICXMakeOfferByCreationTx(submitexthtlc.offerTx);
    if (!offer)
        return Res::Err("order with creation tx %s does not exists!", submitexthtlc.offerTx.GetHex());

    auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
    if (!order)
        return Res::Err("order with creation tx %s does not exists!", offer->orderTx.GetHex());

    if (order->creationHeight + order->expiry < height + (submitexthtlc.timeout * CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS))
        return Res::Err("order will expire before ext htlc expires!");

    if (mnview.HasICXSubmitEXTHTLCOpen(submitexthtlc.offerTx))
        return Res::Err("ext htlc already submitted!");

    if (order->orderType == CICXOrder::TYPE_INTERNAL) {

        if (!HasAuth(offer->ownerAddress))
            return Res::Err("tx must have at least one input from offer owner");

        auto dfchtlc = mnview.HasICXSubmitDFCHTLCOpen(submitexthtlc.offerTx);
        if (!dfchtlc)
            return Res::Err("offer (%s) needs to have dfc htlc submitted first, but no dfc htlc found!", submitexthtlc.offerTx.GetHex());

        auto calcAmount = MultiplyAmounts(dfchtlc->amount, order->orderPrice);
        if (submitexthtlc.amount != calcAmount)
            return Res::Err("amount must be equal to calculated dfchtlc amount");

        if (submitexthtlc.hash != dfchtlc->hash)
            return Res::Err("Invalid hash, external htlc hash is different than dfc htlc hash");

        uint32_t timeout, btcBlocksInDfi;
        if (static_cast<int>(height) < consensus.EunosPayaHeight) {
            timeout = CICXSubmitEXTHTLC::MINIMUM_2ND_TIMEOUT;
            btcBlocksInDfi = CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS;
        } else {
            timeout = CICXSubmitEXTHTLC::EUNOSPAYA_MINIMUM_2ND_TIMEOUT;
            btcBlocksInDfi = CICXSubmitEXTHTLC::EUNOSPAYA_BTC_BLOCKS_IN_DFI_BLOCKS;
        }

        if (submitexthtlc.timeout < timeout)
            return Res::Err("timeout must be greater than %d", timeout - 1);

        if (submitexthtlc.timeout * btcBlocksInDfi >= (dfchtlc->creationHeight + dfchtlc->timeout) - height)
            return Res::Err("timeout must be less than expiration period of 1st htlc in DFC blocks");
    } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {

        if (!HasAuth(order->ownerAddress))
            return Res::Err("tx must have at least one input from order owner");

        if (!mnview.HasICXMakeOfferOpen(offer->orderTx, submitexthtlc.offerTx))
            return Res::Err("offerTx (%s) has expired", submitexthtlc.offerTx.GetHex());

        uint32_t timeout;
        if (static_cast<int>(height) < consensus.EunosPayaHeight)
            timeout = CICXSubmitEXTHTLC::MINIMUM_TIMEOUT;
        else
            timeout = CICXSubmitEXTHTLC::EUNOSPAYA_MINIMUM_TIMEOUT;

        if (submitexthtlc.timeout < timeout)
            return Res::Err("timeout must be greater than %d", timeout - 1);

        CScript offerTxidAddr(offer->creationTx.begin(), offer->creationTx.end());

        auto calcAmount = MultiplyAmounts(submitexthtlc.amount, order->orderPrice);
        if (calcAmount > offer->amount)
            return Res::Err("amount must be lower or equal the offer one");

        CAmount takerFee = offer->takerFee;
        //EunosPaya: calculating adjusted takerFee only if amount in htlc different than in offer
        if (static_cast<int>(height) >= consensus.EunosPayaHeight) {
            if (calcAmount < offer->amount) {
                auto BTCAmount = DivideAmounts(offer->amount, order->orderPrice);
                takerFee = (arith_uint256(submitexthtlc.amount) * offer->takerFee / BTCAmount).GetLow64();
            }
        } else {
            takerFee = CalculateTakerFee(submitexthtlc.amount);
        }

        // refund the rest of locked takerFee if there is difference
        if (offer->takerFee - takerFee) {
            res = TransferTokenBalance(DCT_ID{0}, offer->takerFee - takerFee, offerTxidAddr, offer->ownerAddress);
            if (!res)
                return res;

            // update the offer with adjusted takerFee
            offer->takerFee = takerFee;
            mnview.ICXUpdateMakeOffer(*offer);
        }

        // burn takerFee
        res = TransferTokenBalance(DCT_ID{0}, offer->takerFee, offerTxidAddr, consensus.burnAddress);
        if (!res)
            return res;

        // burn makerDeposit
        res = TransferTokenBalance(DCT_ID{0}, offer->takerFee, order->ownerAddress, consensus.burnAddress);
    }

    return !res ? res : mnview.ICXSubmitEXTHTLC(submitexthtlc);
}

Res CICXOrdersConsensus::operator()(const CICXClaimDFCHTLCMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    CICXClaimDFCHTLCImplemetation claimdfchtlc;
    static_cast<CICXClaimDFCHTLC&>(claimdfchtlc) = obj;

    claimdfchtlc.creationTx = tx.GetHash();
    claimdfchtlc.creationHeight = height;

    auto dfchtlc = mnview.GetICXSubmitDFCHTLCByCreationTx(claimdfchtlc.dfchtlcTx);
    if (!dfchtlc)
        return Res::Err("dfc htlc with creation tx %s does not exists!", claimdfchtlc.dfchtlcTx.GetHex());

    if (!mnview.HasICXSubmitDFCHTLCOpen(dfchtlc->offerTx))
        return Res::Err("dfc htlc not found or already claimed or refunded!");

    uint256 calcHash;
    uint8_t calcSeedBytes[32];
    CSHA256()
        .Write(claimdfchtlc.seed.data(), claimdfchtlc.seed.size())
        .Finalize(calcSeedBytes);
    calcHash.SetHex(HexStr(calcSeedBytes, calcSeedBytes + 32));

    if (dfchtlc->hash != calcHash)
        return Res::Err("hash generated from given seed is different than in dfc htlc: %s - %s!", calcHash.GetHex(), dfchtlc->hash.GetHex());

    auto offer = mnview.GetICXMakeOfferByCreationTx(dfchtlc->offerTx);
    if (!offer)
        return Res::Err("offer with creation tx %s does not exists!", dfchtlc->offerTx.GetHex());

    auto order = mnview.GetICXOrderByCreationTx(offer->orderTx);
    if (!order)
        return Res::Err("order with creation tx %s does not exists!", offer->orderTx.GetHex());

    auto exthtlc = mnview.HasICXSubmitEXTHTLCOpen(dfchtlc->offerTx);
    if (static_cast<int>(height) < consensus.EunosPayaHeight && !exthtlc)
        return Res::Err("cannot claim, external htlc for this offer does not exists or expired!");

    // claim DFC HTLC to receiveAddress
    CScript htlcTxidAddr(dfchtlc->creationTx.begin(), dfchtlc->creationTx.end());

    if (order->orderType == CICXOrder::TYPE_INTERNAL)
        res = TransferTokenBalance(order->idToken, dfchtlc->amount, htlcTxidAddr, offer->ownerAddress);
    else if (order->orderType == CICXOrder::TYPE_EXTERNAL)
        res = TransferTokenBalance(order->idToken, dfchtlc->amount, htlcTxidAddr, order->ownerAddress);

    if (!res)
        return res;

    // refund makerDeposit
    res = mnview.AddBalancePlusRewards(order->ownerAddress, {DCT_ID{0}, offer->takerFee}, height);
    if (!res)
        return res;

    // makerIncentive
    res = mnview.AddBalancePlusRewards(order->ownerAddress, {DCT_ID{0}, offer->takerFee * 25 / 100}, height);
    if (!res)
        return res;

    // maker bonus only on fair dBTC/BTC (1:1) trades for now
    auto tokenBTC = mnview.GetToken(CICXOrder::TOKEN_BTC);
    assert(tokenBTC);
    if (order->idToken == tokenBTC->first && order->orderPrice == COIN) {
        // maker bonus should be in DFI
        auto tokenId = static_cast<int>(height) < consensus.GreatWorldHeight ? tokenBTC->first : DCT_ID{0};
        res = mnview.AddBalancePlusRewards(order->ownerAddress, {tokenId, offer->takerFee * 50 / 100}, height);
        if (!res)
            return res;
    }

    if (order->orderType == CICXOrder::TYPE_INTERNAL)
        order->amountToFill -= dfchtlc->amount;
    else if (order->orderType == CICXOrder::TYPE_EXTERNAL)
        order->amountToFill -= DivideAmounts(dfchtlc->amount, order->orderPrice);

    // Order fulfilled, close order.
    if (order->amountToFill == 0) {
        order->closeTx = claimdfchtlc.creationTx;
        order->closeHeight = height;
        res = mnview.ICXCloseOrderTx(*order, CICXOrder::STATUS_FILLED);
        if (!res)
            return res;
    }

    res = mnview.ICXClaimDFCHTLC(claimdfchtlc,offer->creationTx,*order);
    if (!res)
        return res;
    // Close offer
    res = mnview.ICXCloseMakeOfferTx(*offer, CICXMakeOffer::STATUS_CLOSED);
    if (!res)
        return res;
    res = mnview.ICXCloseDFCHTLC(*dfchtlc, CICXSubmitDFCHTLC::STATUS_CLAIMED);
    if (!res)
        return res;

    if (static_cast<int>(height) >= consensus.EunosPayaHeight) {
        if (exthtlc)
            return mnview.ICXCloseEXTHTLC(*exthtlc, CICXSubmitEXTHTLC::STATUS_CLOSED);

        return Res::Ok();
    }

    return mnview.ICXCloseEXTHTLC(*exthtlc, CICXSubmitEXTHTLC::STATUS_CLOSED);
}

Res CICXOrdersConsensus::operator()(const CICXCloseOrderMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    CICXCloseOrderImplemetation closeorder;
    static_cast<CICXCloseOrder&>(closeorder) = obj;

    closeorder.creationTx = tx.GetHash();
    closeorder.creationHeight = height;

    std::optional<CICXOrderImplemetation> order;
    if (!(order = mnview.GetICXOrderByCreationTx(closeorder.orderTx)))
        return Res::Err("order with creation tx %s does not exists!", closeorder.orderTx.GetHex());

    if (!order->closeTx.IsNull())
        return Res::Err("order with creation tx %s is already closed!", closeorder.orderTx.GetHex());

    if (!mnview.HasICXOrderOpen(order->idToken, order->creationTx))
        return Res::Err("order with creation tx %s is already closed!", closeorder.orderTx.GetHex());

    // check auth
    if (!HasAuth(order->ownerAddress))
        return Res::Err("tx must have at least one input from order owner");

    order->closeTx = closeorder.creationTx;
    order->closeHeight = closeorder.creationHeight;

    if (order->orderType == CICXOrder::TYPE_INTERNAL && order->amountToFill > 0) {
        // subtract the balance from txidAddr and return to owner
        CScript txidAddr(order->creationTx.begin(), order->creationTx.end());
        res = TransferTokenBalance(order->idToken, order->amountToFill, txidAddr, order->ownerAddress);
        if (!res)
            return res;
    }

    res = mnview.ICXCloseOrder(closeorder);
    return !res ? res : mnview.ICXCloseOrderTx(*order,CICXOrder::STATUS_CLOSED);
}

Res CICXOrdersConsensus::operator()(const CICXCloseOfferMessage& obj) const {
    auto res = CheckCustomTx();
    if (!res)
        return res;

    CICXCloseOfferImplemetation closeoffer;
    static_cast<CICXCloseOffer&>(closeoffer) = obj;

    closeoffer.creationTx = tx.GetHash();
    closeoffer.creationHeight = height;

    std::optional<CICXMakeOfferImplemetation> offer;
    if (!(offer = mnview.GetICXMakeOfferByCreationTx(closeoffer.offerTx)))
        return Res::Err("offer with creation tx %s does not exists!", closeoffer.offerTx.GetHex());

    if (!offer->closeTx.IsNull())
        return Res::Err("offer with creation tx %s is already closed!", closeoffer.offerTx.GetHex());

    if (!mnview.HasICXMakeOfferOpen(offer->orderTx, offer->creationTx))
        return Res::Err("offer with creation tx %s does not exists!", closeoffer.offerTx.GetHex());

    std::optional<CICXOrderImplemetation> order;
    if (!(order = mnview.GetICXOrderByCreationTx(offer->orderTx)))
        return Res::Err("order with creation tx %s does not exists!", offer->orderTx.GetHex());

    // check auth
    if (!HasAuth(offer->ownerAddress))
        return Res::Err("tx must have at least one input from offer owner");

    offer->closeTx = closeoffer.creationTx;
    offer->closeHeight = closeoffer.creationHeight;

    bool isPreEunosPaya = static_cast<int>(height) < consensus.EunosPayaHeight;

    if (order->orderType == CICXOrder::TYPE_INTERNAL && !mnview.ExistedICXSubmitDFCHTLC(offer->creationTx, isPreEunosPaya)) {
        // subtract takerFee from txidAddr and return to owner
        CScript txidAddr(offer->creationTx.begin(), offer->creationTx.end());
        res = TransferTokenBalance(DCT_ID{0}, offer->takerFee, txidAddr, offer->ownerAddress);
        if (!res)
            return res;

    } else if (order->orderType == CICXOrder::TYPE_EXTERNAL) {
        // subtract the balance from txidAddr and return to owner
        CScript txidAddr(offer->creationTx.begin(), offer->creationTx.end());
        if (isPreEunosPaya) {
            res = TransferTokenBalance(order->idToken, offer->amount, txidAddr, offer->ownerAddress);
            if (!res)
                return res;
        }
        if (!mnview.ExistedICXSubmitEXTHTLC(offer->creationTx, isPreEunosPaya)) {
            res = TransferTokenBalance(DCT_ID{0}, offer->takerFee, txidAddr, offer->ownerAddress);
            if (!res)
                return res;
        }
    }

    res = mnview.ICXCloseOffer(closeoffer);
    return !res ? res : mnview.ICXCloseMakeOfferTx(*offer, CICXMakeOffer::STATUS_CLOSED);
}
