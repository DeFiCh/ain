#include <masternodes/icxorder.h>

std::optional<CICXOrderView::CICXOrderImpl> CICXOrderView::GetICXOrderByCreationTx(uint256 const & txid) const
{
    return ReadBy<ICXOrderCreationTx, CICXOrderImpl>(txid);
}

uint8_t CICXOrderView::GetICXOrderStatus(OrderKey const & key) const
{
    auto status = ReadBy<ICXOrderOpenKey, uint8_t>(key);

    if (!status)
        status = ReadBy<ICXOrderCloseKey, uint8_t>(key);

    if (status)
        return (*status);

    return {};
}

Res CICXOrderView::ICXCreateOrder(CICXOrderImpl const & order)
{
    //this should not happen, but for sure
    Require(!GetICXOrderByCreationTx(order.creationTx), "order with creation tx %s already exists!", order.creationTx.GetHex());
    Require(order.orderType == CICXOrder::TYPE_INTERNAL || order.orderType == CICXOrder::TYPE_EXTERNAL, "invalid order type!");
    Require(order.amountFrom != 0, "order amountFrom must be greater than 0!");
    Require(order.amountToFill == order.amountFrom, "order amountToFill does not equal to amountFrom!");
    Require(order.orderPrice != 0, "order price must be greater than 0!");
    Require(order.expiry >= CICXOrder::DEFAULT_EXPIRY, "order expiry must be greater than %d!", CICXOrder::DEFAULT_EXPIRY - 1);

    OrderKey key(order.idToken, order.creationTx);
    WriteBy<ICXOrderCreationTx>(order.creationTx, order);
    WriteBy<ICXOrderOpenKey>(key, CICXOrder::STATUS_OPEN);
    WriteBy<ICXOrderStatus>(StatusKey(order.creationHeight + order.expiry, order.creationTx), CICXOrder::STATUS_EXPIRED);

    return Res::Ok();
}

Res CICXOrderView::ICXUpdateOrder(CICXOrderImpl const & order)
{
    //this should not happen, but for sure
    Require(GetICXOrderByCreationTx(order.creationTx), "order with creation tx %s doesn't exists!", order.creationTx.GetHex());

    OrderKey key(order.idToken, order.creationTx);
    WriteBy<ICXOrderCreationTx>(order.creationTx, order);

    return Res::Ok();
}

Res CICXOrderView::ICXCloseOrderTx(CICXOrderImpl const & order, uint8_t const status)
{
    WriteBy<ICXOrderCreationTx>(order.creationTx, order);
    OrderKey key(order.idToken, order.creationTx);
    EraseBy<ICXOrderOpenKey>(key);
    WriteBy<ICXOrderCloseKey>(key, status);
    EraseBy<ICXOrderStatus>(StatusKey(order.creationHeight + order.expiry, order.creationTx));

    return Res::Ok();
}

void CICXOrderView::ForEachICXOrderOpen(std::function<bool (OrderKey const &, uint8_t)> callback, DCT_ID const & id)
{
    ForEach<ICXOrderOpenKey, OrderKey, uint8_t>(callback, OrderKey{id, {}});
}

void CICXOrderView::ForEachICXOrderClose(std::function<bool (OrderKey const &, uint8_t)> callback, DCT_ID const & id)
{
    ForEach<ICXOrderCloseKey, OrderKey, uint8_t>(callback, OrderKey{id, {}});
}

void CICXOrderView::ForEachICXOrderExpire(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height)
{
    ForEach<ICXOrderStatus, StatusKey, uint8_t>(callback, StatusKey{height, {}});
}

std::optional<CICXOrderView::CICXOrderImpl> CICXOrderView::HasICXOrderOpen(DCT_ID const & tokenId, uint256 const & ordertxid)
{
    if (ExistsBy<ICXOrderOpenKey>(OrderKey{tokenId, ordertxid}))
        return GetICXOrderByCreationTx(ordertxid);
    return {};
}

std::optional<CICXOrderView::CICXMakeOfferImpl> CICXOrderView::GetICXMakeOfferByCreationTx(uint256 const & txid) const
{
    return ReadBy<ICXMakeOfferCreationTx, CICXMakeOfferImpl>(txid);
}

uint8_t CICXOrderView::GetICXMakeOfferStatus(TxidPairKey const & key) const
{
    auto status = ReadBy<ICXMakeOfferOpenKey, uint8_t>(key);

    if (!status)
        status = ReadBy<ICXMakeOfferCloseKey, uint8_t>(key);

    if (status)
        return (*status);

    return {};
}

Res CICXOrderView::ICXMakeOffer(CICXMakeOfferImpl const & makeoffer)
{
    //this should not happen, but for sure
    Require(!GetICXMakeOfferByCreationTx(makeoffer.creationTx), "makeoffer with creation tx %s already exists!", makeoffer.creationTx.GetHex());
    Require(makeoffer.amount != 0, "offer amount must be greater than 0!");

    WriteBy<ICXMakeOfferCreationTx>(makeoffer.creationTx, makeoffer);
    WriteBy<ICXMakeOfferOpenKey>(TxidPairKey(makeoffer.orderTx, makeoffer.creationTx), CICXMakeOffer::STATUS_OPEN);
    WriteBy<ICXOfferStatus>(StatusKey(makeoffer.creationHeight + makeoffer.expiry, makeoffer.creationTx), CICXMakeOffer::STATUS_EXPIRED);

    return Res::Ok();
}

Res CICXOrderView::ICXUpdateMakeOffer(CICXMakeOfferImpl const & makeoffer)
{
    WriteBy<ICXMakeOfferCreationTx>(makeoffer.creationTx, makeoffer);

    return Res::Ok();
}

Res CICXOrderView::ICXCloseMakeOfferTx(CICXMakeOfferImpl const & makeoffer, uint8_t const status)
{
    TxidPairKey key(makeoffer.orderTx,makeoffer.creationTx);
    EraseBy<ICXMakeOfferOpenKey>(key);
    WriteBy<ICXMakeOfferCloseKey>(key, status);
    EraseBy<ICXOfferStatus>(StatusKey(makeoffer.creationHeight + makeoffer.expiry, makeoffer.creationTx));

    return Res::Ok();
}

void CICXOrderView::ForEachICXMakeOfferOpen(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & txid)
{
    ForEach<ICXMakeOfferOpenKey, TxidPairKey, uint8_t>(callback, TxidPairKey{txid, {}});
}

void CICXOrderView::ForEachICXMakeOfferClose(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & txid)
{
    ForEach<ICXMakeOfferCloseKey, TxidPairKey, uint8_t>(callback, TxidPairKey{txid, {}});
}

void CICXOrderView::ForEachICXMakeOfferExpire(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height)
{
    ForEach<ICXOfferStatus, StatusKey, uint8_t>(callback, StatusKey{height, {}});
}

std::optional<CICXOrderView::CICXMakeOfferImpl> CICXOrderView::HasICXMakeOfferOpen(uint256 const & ordertxid, uint256 const & offertxid)
{
    if (ExistsBy<ICXMakeOfferOpenKey>(TxidPairKey{ordertxid, offertxid}))
        return GetICXMakeOfferByCreationTx(offertxid);
    return {};
}

std::optional<CICXOrderView::CICXSubmitDFCHTLCImpl> CICXOrderView::GetICXSubmitDFCHTLCByCreationTx(uint256 const & txid) const
{
    return ReadBy<ICXSubmitDFCHTLCCreationTx, CICXSubmitDFCHTLCImpl>(txid);
}

Res CICXOrderView::ICXSubmitDFCHTLC(CICXSubmitDFCHTLCImpl const & submitdfchtlc)
{
    //this should not happen, but for sure
    Require(!GetICXSubmitDFCHTLCByCreationTx(submitdfchtlc.creationTx), "submitdfchtlc with creation tx %s already exists!", submitdfchtlc.creationTx.GetHex());
    Require(submitdfchtlc.amount != 0, "Invalid amount, must be greater than 0!");
    Require(!submitdfchtlc.hash.IsNull(), "Invalid hash, htlc hash is empty and it must be set!");
    Require(submitdfchtlc.timeout != 0, "Invalid timeout, must be greater than 0!");

    WriteBy<ICXSubmitDFCHTLCCreationTx>(submitdfchtlc.creationTx, submitdfchtlc);
    WriteBy<ICXSubmitDFCHTLCOpenKey>(TxidPairKey(submitdfchtlc.offerTx, submitdfchtlc.creationTx), CICXSubmitDFCHTLC::STATUS_OPEN);
    WriteBy<ICXSubmitDFCHTLCStatus>(StatusKey(submitdfchtlc.creationHeight + CICXMakeOffer::MAKER_DEPOSIT_REFUND_TIMEOUT, submitdfchtlc.creationTx), CICXSubmitDFCHTLC::STATUS_EXPIRED);
    WriteBy<ICXSubmitDFCHTLCStatus>(StatusKey(submitdfchtlc.creationHeight + submitdfchtlc.timeout, submitdfchtlc.creationTx), CICXSubmitDFCHTLC::STATUS_REFUNDED);

    return Res::Ok();
}

Res CICXOrderView::ICXCloseDFCHTLC(CICXSubmitDFCHTLCImpl const & submitdfchtlc, uint8_t const status)
{
    WriteBy<ICXSubmitDFCHTLCCreationTx>(submitdfchtlc.creationTx, submitdfchtlc);
    EraseBy<ICXSubmitDFCHTLCOpenKey>(TxidPairKey(submitdfchtlc.offerTx, submitdfchtlc.creationTx));
    WriteBy<ICXSubmitDFCHTLCCloseKey>(TxidPairKey(submitdfchtlc.offerTx, submitdfchtlc.creationTx), status);

    EraseBy<ICXSubmitDFCHTLCStatus>(StatusKey(submitdfchtlc.creationHeight + CICXMakeOffer::MAKER_DEPOSIT_REFUND_TIMEOUT, submitdfchtlc.creationTx));
    EraseBy<ICXSubmitDFCHTLCStatus>(StatusKey(submitdfchtlc.creationHeight + submitdfchtlc.timeout, submitdfchtlc.creationTx));

    return Res::Ok();
}

void CICXOrderView::ForEachICXSubmitDFCHTLCOpen(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid)
{
    ForEach<ICXSubmitDFCHTLCOpenKey, TxidPairKey, uint8_t>(callback, TxidPairKey{offertxid, {}});
}

void CICXOrderView::ForEachICXSubmitDFCHTLCClose(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid)
{
    ForEach<ICXSubmitDFCHTLCCloseKey, TxidPairKey, uint8_t>(callback, TxidPairKey{offertxid, {}});
}

void CICXOrderView::ForEachICXSubmitDFCHTLCExpire(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height)
{
    ForEach<ICXSubmitDFCHTLCStatus, StatusKey, uint8_t>(callback, StatusKey{height, {}});
}

std::optional<CICXOrderView::CICXSubmitDFCHTLCImpl> CICXOrderView::HasICXSubmitDFCHTLCOpen(uint256 const & offertxid)
{
    auto it = LowerBound<ICXSubmitDFCHTLCOpenKey>(TxidPairKey{offertxid, {}});
    if (it.Valid() && it.Key().first == offertxid)
        return GetICXSubmitDFCHTLCByCreationTx(it.Key().second);
    return {};
}

bool CICXOrderView::ExistedICXSubmitDFCHTLC(uint256 const & offertxid, bool isPreEunosPaya)
{
    bool result = false;

    if (HasICXSubmitDFCHTLCOpen(offertxid))
        result = true;

    if (isPreEunosPaya)
        return (result);

    auto it = LowerBound<ICXSubmitDFCHTLCCloseKey>(TxidPairKey{offertxid, {}});
    if (it.Valid() && it.Key().first == offertxid)
        result = true;

    return (result);
}

std::optional<CICXOrderView::CICXSubmitEXTHTLCImpl> CICXOrderView::GetICXSubmitEXTHTLCByCreationTx(const uint256 & txid) const
{
    return ReadBy<ICXSubmitEXTHTLCCreationTx,CICXSubmitEXTHTLCImpl>(txid);
}

Res CICXOrderView::ICXSubmitEXTHTLC(CICXSubmitEXTHTLCImpl const & submitexthtlc)
{
    //this should not happen, but for sure
    Require(!GetICXSubmitEXTHTLCByCreationTx(submitexthtlc.creationTx), "submitexthtlc with creation tx %s already exists!", submitexthtlc.creationTx.GetHex());
    Require(submitexthtlc.amount != 0, "Invalid amount, must be greater than 0!");
    Require(!submitexthtlc.htlcscriptAddress.empty(), "Invalid htlcscriptAddress, htlcscriptAddress is empty and it must be set!");
    Require(!submitexthtlc.hash.IsNull(), "Invalid hash, htlc hash is empty and it must be set!");
    Require(submitexthtlc.ownerPubkey.IsFullyValid(), "Invalid refundPubkey is not a valid pubkey!");
    Require(submitexthtlc.timeout != 0, "Invalid timout, must be greater than 0!");

    WriteBy<ICXSubmitEXTHTLCCreationTx>(submitexthtlc.creationTx, submitexthtlc);
    WriteBy<ICXSubmitEXTHTLCOpenKey>(TxidPairKey(submitexthtlc.offerTx, submitexthtlc.creationTx), CICXSubmitEXTHTLC::STATUS_OPEN);
    WriteBy<ICXSubmitEXTHTLCStatus>(StatusKey(submitexthtlc.creationHeight + CICXMakeOffer::MAKER_DEPOSIT_REFUND_TIMEOUT, submitexthtlc.creationTx), CICXSubmitEXTHTLC::STATUS_EXPIRED);
    return Res::Ok();
}

Res CICXOrderView::ICXCloseEXTHTLC(CICXSubmitEXTHTLCImpl const & submitexthtlc, uint8_t const status)
{
    WriteBy<ICXSubmitEXTHTLCCreationTx>(submitexthtlc.creationTx, submitexthtlc);
    EraseBy<ICXSubmitEXTHTLCOpenKey>(TxidPairKey(submitexthtlc.offerTx, submitexthtlc.creationTx));
    WriteBy<ICXSubmitEXTHTLCCloseKey>(TxidPairKey(submitexthtlc.offerTx, submitexthtlc.creationTx), status);
    EraseBy<ICXSubmitEXTHTLCStatus>(StatusKey(submitexthtlc.creationHeight + CICXMakeOffer::MAKER_DEPOSIT_REFUND_TIMEOUT, submitexthtlc.creationTx));

    return Res::Ok();
}

void CICXOrderView::ForEachICXSubmitEXTHTLCOpen(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid)
{
    ForEach<ICXSubmitEXTHTLCOpenKey,TxidPairKey,uint8_t>(callback, TxidPairKey{offertxid, {}});
}

void CICXOrderView::ForEachICXSubmitEXTHTLCClose(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid)
{
    ForEach<ICXSubmitEXTHTLCCloseKey, TxidPairKey, uint8_t>(callback, TxidPairKey{offertxid, {}});
}

void CICXOrderView::ForEachICXSubmitEXTHTLCExpire(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height)
{
    ForEach<ICXSubmitEXTHTLCStatus, StatusKey, uint8_t>(callback, StatusKey{height, {}});
}

std::optional<CICXOrderView::CICXSubmitEXTHTLCImpl> CICXOrderView::HasICXSubmitEXTHTLCOpen(uint256 const & offertxid)
{
    auto it = LowerBound<ICXSubmitEXTHTLCOpenKey>(TxidPairKey{offertxid, {}});
    if (it.Valid() && it.Key().first == offertxid)
        return GetICXSubmitEXTHTLCByCreationTx(it.Key().second);
    return {};
}

bool CICXOrderView::ExistedICXSubmitEXTHTLC(uint256 const & offertxid, bool isPreEunosPaya)
{
    bool result = false;

    if (HasICXSubmitEXTHTLCOpen(offertxid))
        result = true;

    if (isPreEunosPaya)
        return (result);

    auto it = LowerBound<ICXSubmitEXTHTLCCloseKey>(TxidPairKey{offertxid, {}});
    if (it.Valid() && it.Key().first == offertxid)
        result = true;

    return (result);
}

std::optional<CICXOrderView::CICXClaimDFCHTLCImpl> CICXOrderView::GetICXClaimDFCHTLCByCreationTx(uint256 const & txid) const
{
    return ReadBy<ICXClaimDFCHTLCCreationTx, CICXClaimDFCHTLCImpl>(txid);
}

Res CICXOrderView::ICXClaimDFCHTLC(CICXClaimDFCHTLCImpl const & claimdfchtlc, uint256 const & offertxid, CICXOrderImpl const & order)
{
    //this should not happen, but for sure
    Require(!GetICXClaimDFCHTLCByCreationTx(claimdfchtlc.creationTx), "claimdfchtlc with creation tx %s already exists!", claimdfchtlc.creationTx.GetHex());

    WriteBy<ICXClaimDFCHTLCCreationTx>(claimdfchtlc.creationTx, claimdfchtlc);
    WriteBy<ICXClaimDFCHTLCKey>(TxidPairKey(offertxid, claimdfchtlc.creationTx),CICXSubmitDFCHTLC::STATUS_CLAIMED);

    if (order.amountToFill != 0)
        WriteBy<ICXOrderCreationTx>(order.creationTx, order);

    return Res::Ok();
}

void CICXOrderView::ForEachICXClaimDFCHTLC(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid)
{
     ForEach<ICXClaimDFCHTLCKey, TxidPairKey, uint8_t>(callback, TxidPairKey{offertxid, {}});
}

std::optional<CICXOrderView::CICXCloseOrderImpl> CICXOrderView::GetICXCloseOrderByCreationTx(uint256 const & txid) const
{
    return ReadBy<ICXCloseOrderCreationTx, CICXCloseOrderImpl>(txid);
}

Res CICXOrderView::ICXCloseOrder(CICXCloseOrderImpl const & closeorder)
{
    //this should not happen, but for sure
    Require(!GetICXCloseOrderByCreationTx(closeorder.creationTx), "closeorder with creation tx %s already exists!", closeorder.creationTx.GetHex());

    WriteBy<ICXCloseOrderCreationTx>(closeorder.creationTx, closeorder.orderTx);

    return Res::Ok();
}

std::optional<CICXOrderView::CICXCloseOfferImpl> CICXOrderView::GetICXCloseOfferByCreationTx(uint256 const & txid) const
{
    return ReadBy<ICXCloseOfferCreationTx, CICXCloseOfferImpl>(txid);
}

Res CICXOrderView::ICXCloseOffer(CICXCloseOfferImpl const & closeoffer)
{
    //this should not happen, but for sure
    Require(!GetICXCloseOrderByCreationTx(closeoffer.creationTx), "closeooffer with creation tx %s already exists!", closeoffer.creationTx.GetHex());

    WriteBy<ICXCloseOfferCreationTx>(closeoffer.creationTx, closeoffer.offerTx);

    return Res::Ok();
}

Res CICXOrderView::ICXSetTakerFeePerBTC(CAmount amount)
{
    WriteBy<ICXVariables>('A', amount);

    return Res::Ok();
}

Res CICXOrderView::ICXEraseTakerFeePerBTC()
{
    EraseBy<ICXVariables>('A');

    return Res::Ok();
}

CAmount CICXOrderView::ICXGetTakerFeePerBTC()
{
    CAmount takerFeePerBTC = CICXMakeOffer::DEFAULT_TAKER_FEE_PER_BTC;

    auto fee = ReadBy<ICXVariables, CAmount>('A');
    if (fee)
        takerFeePerBTC = *fee;

    return (takerFeePerBTC);
}
