#include <masternodes/icxorder.h>
#include <rpc/util.h> /// AmountFromValue
#include <core_io.h> /// ValueFromAmount

/// @attention make sure that it does not overlap with other views !!!
const unsigned char CICXOrderView::ICXOrderCreationTx           ::prefix = '1';
const unsigned char CICXOrderView::ICXMakeOfferCreationTx       ::prefix = '2';
const unsigned char CICXOrderView::ICXSubmitDFCHTLCCreationTx   ::prefix = '3';
const unsigned char CICXOrderView::ICXSubmitEXTHTLCCreationTx   ::prefix = '4';
const unsigned char CICXOrderView::ICXClaimDFCHTLCCreationTx    ::prefix = '5';
const unsigned char CICXOrderView::ICXCloseOrderCreationTx      ::prefix = '6';
const unsigned char CICXOrderView::ICXCloseOfferCreationTx      ::prefix = '7';

const unsigned char CICXOrderView::ICXOrderOpenKey              ::prefix = 0x01;
const unsigned char CICXOrderView::ICXOrderCloseKey             ::prefix = 0x02;
const unsigned char CICXOrderView::ICXMakeOfferOpenKey          ::prefix = 0x03;
const unsigned char CICXOrderView::ICXMakeOfferCloseKey         ::prefix = 0x04;
const unsigned char CICXOrderView::ICXSubmitDFCHTLCOpenKey      ::prefix = 0x05;
const unsigned char CICXOrderView::ICXSubmitDFCHTLCCloseKey     ::prefix = 0x06;
const unsigned char CICXOrderView::ICXSubmitEXTHTLCOpenKey      ::prefix = 0x07;
const unsigned char CICXOrderView::ICXSubmitEXTHTLCCloseKey     ::prefix = 0x08;
const unsigned char CICXOrderView::ICXClaimDFCHTLCKey           ::prefix = 0x09;

const unsigned char CICXOrderView::ICXOrderStatus               ::prefix = 0x0A;
const unsigned char CICXOrderView::ICXOfferStatus               ::prefix = 0x0B;
const unsigned char CICXOrderView::ICXSubmitDFCHTLCStatus       ::prefix = 0x0C;
const unsigned char CICXOrderView::ICXSubmitEXTHTLCStatus       ::prefix = 0x0D;

const unsigned char CICXOrderView::ICXVariables                 ::prefix = 0x0F;

const uint32_t CICXOrder::DEFAULT_EXPIRY = 2880;
const uint8_t CICXOrder::TYPE_INTERNAL = 1;
const uint8_t CICXOrder::TYPE_EXTERNAL = 2;
const uint8_t CICXOrder::STATUS_OPEN = 0;
const uint8_t CICXOrder::STATUS_CLOSED = 1;
const uint8_t CICXOrder::STATUS_FILLED = 2;
const uint8_t CICXOrder::STATUS_EXPIRED = 3;
const std::string CICXOrder::CHAIN_BTC = "BTC";
const std::string CICXOrder::TOKEN_BTC = "BTC";

const uint32_t CICXMakeOffer::DEFAULT_EXPIRY = 10;
const uint32_t CICXMakeOffer::EUNOSPAYA_DEFAULT_EXPIRY = 20;
const uint32_t CICXMakeOffer::MAKER_DEPOSIT_REFUND_TIMEOUT = 100;
const uint8_t CICXMakeOffer::STATUS_OPEN = 0;
const uint8_t CICXMakeOffer::STATUS_CLOSED = 1;
const uint8_t CICXMakeOffer::STATUS_EXPIRED = 2;
const CAmount CICXMakeOffer::DEFAULT_TAKER_FEE_PER_BTC = AmountFromValue(0.003);

const uint32_t CICXSubmitDFCHTLC::MINIMUM_TIMEOUT = 500;
const uint32_t CICXSubmitDFCHTLC::MINIMUM_2ND_TIMEOUT = 250;
const uint32_t CICXSubmitDFCHTLC::EUNOSPAYA_MINIMUM_TIMEOUT = 1440;
const uint32_t CICXSubmitDFCHTLC::EUNOSPAYA_MINIMUM_2ND_TIMEOUT = 480;
const uint8_t CICXSubmitDFCHTLC::STATUS_OPEN = 0;
const uint8_t CICXSubmitDFCHTLC::STATUS_CLAIMED = 1;
const uint8_t CICXSubmitDFCHTLC::STATUS_REFUNDED = 2;
const uint8_t CICXSubmitDFCHTLC::STATUS_EXPIRED = 3;

const uint32_t CICXSubmitEXTHTLC::MINIMUM_TIMEOUT = 30;
const uint32_t CICXSubmitEXTHTLC::MINIMUM_2ND_TIMEOUT = 15;
const uint32_t CICXSubmitEXTHTLC::EUNOSPAYA_MINIMUM_TIMEOUT = 72;
const uint32_t CICXSubmitEXTHTLC::EUNOSPAYA_MINIMUM_2ND_TIMEOUT = 24;
// constant for calculating BTC block period in DFI block period per hour (BTC estimated to 6 blocks/h, DFI to 96 blocks/h)
const uint32_t CICXSubmitEXTHTLC::BTC_BLOCKS_IN_DFI_BLOCKS = 16;
const uint32_t CICXSubmitEXTHTLC::EUNOSPAYA_BTC_BLOCKS_IN_DFI_BLOCKS = 20;
const uint8_t CICXSubmitEXTHTLC::STATUS_OPEN = 0;
const uint8_t CICXSubmitEXTHTLC::STATUS_CLOSED = 1;
const uint8_t CICXSubmitEXTHTLC::STATUS_EXPIRED = 3;

const CAmount CICXOrderView::DEFAULT_DFI_BTC_PRICE = 15000;

std::unique_ptr<CICXOrderView::CICXOrderImpl> CICXOrderView::GetICXOrderByCreationTx(uint256 const & txid) const
{
    auto order = ReadBy<ICXOrderCreationTx,CICXOrderImpl>(txid);
    if (order)
        return MakeUnique<CICXOrderImpl>(*order);
    return {};
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
    if (GetICXOrderByCreationTx(order.creationTx))
        return Res::Err("order with creation tx %s already exists!", order.creationTx.GetHex());
    if (order.orderType != CICXOrder::TYPE_INTERNAL && order.orderType != CICXOrder::TYPE_EXTERNAL)
        return Res::Err("invalid order type!");
    if (order.amountFrom == 0)
        return Res::Err("order amountFrom must be greater than 0!");
    if (order.amountToFill != order.amountFrom)
        return Res::Err("order amountToFill does not equal to amountFrom!");
    if (order.orderPrice == 0)
        return Res::Err("order price must be greater than 0!");
    if (order.expiry < CICXOrder::DEFAULT_EXPIRY)
        return Res::Err("order expiry must be greater than %d!", CICXOrder::DEFAULT_EXPIRY - 1);

    OrderKey key(order.idToken, order.creationTx);
    WriteBy<ICXOrderCreationTx>(order.creationTx, order);
    WriteBy<ICXOrderOpenKey>(key, CICXOrder::STATUS_OPEN);
    WriteBy<ICXOrderStatus>(StatusKey(order.creationHeight + order.expiry, order.creationTx), CICXOrder::STATUS_EXPIRED);

    return Res::Ok();
}

Res CICXOrderView::ICXUpdateOrder(CICXOrderImpl const & order)
{
    //this should not happen, but for sure
    if (!GetICXOrderByCreationTx(order.creationTx))
        return Res::Err("order with creation tx %s doesn't exists!", order.creationTx.GetHex());

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

std::unique_ptr<CICXOrderView::CICXOrderImpl> CICXOrderView::HasICXOrderOpen(DCT_ID const & tokenId, uint256 const & ordertxid)
{
    auto it = LowerBound<ICXOrderOpenKey>(OrderKey{tokenId, ordertxid});
    if (it.Valid() && it.Key().first == tokenId && it.Key().second == ordertxid)
        return GetICXOrderByCreationTx(it.Key().second);
    return {};
}


std::unique_ptr<CICXOrderView::CICXMakeOfferImpl> CICXOrderView::GetICXMakeOfferByCreationTx(uint256 const & txid) const
{
    auto makeoffer = ReadBy<ICXMakeOfferCreationTx,CICXMakeOfferImpl>(txid);
    if (makeoffer)
        return MakeUnique<CICXMakeOfferImpl>(*makeoffer);
    return {};
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
    if (GetICXMakeOfferByCreationTx(makeoffer.creationTx))
        return Res::Err("makeoffer with creation tx %s already exists!", makeoffer.creationTx.GetHex());
    if (makeoffer.amount == 0)
        return Res::Err("offer amount must be greater than 0!");

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

std::unique_ptr<CICXOrderView::CICXMakeOfferImpl> CICXOrderView::HasICXMakeOfferOpen(uint256 const & ordertxid, uint256 const & offertxid)
{
    auto it = LowerBound<ICXMakeOfferOpenKey>(TxidPairKey{ordertxid, offertxid});
    if (it.Valid() && it.Key().first == ordertxid && it.Key().second == offertxid)
        return GetICXMakeOfferByCreationTx(it.Key().second);
    return {};
}

std::unique_ptr<CICXOrderView::CICXSubmitDFCHTLCImpl> CICXOrderView::GetICXSubmitDFCHTLCByCreationTx(uint256 const & txid) const
{
    auto submitdfchtlc = ReadBy<ICXSubmitDFCHTLCCreationTx,CICXSubmitDFCHTLCImpl>(txid);
    if (submitdfchtlc)
        return MakeUnique<CICXSubmitDFCHTLCImpl>(*submitdfchtlc);
    return {};
}

Res CICXOrderView::ICXSubmitDFCHTLC(CICXSubmitDFCHTLCImpl const & submitdfchtlc)
{
    //this should not happen, but for sure
    if (GetICXSubmitDFCHTLCByCreationTx(submitdfchtlc.creationTx))
        return Res::Err("submitdfchtlc with creation tx %s already exists!", submitdfchtlc.creationTx.GetHex());
    if (submitdfchtlc.amount == 0)
        return Res::Err("Invalid amount, must be greater than 0!");
    if (submitdfchtlc.hash.IsNull())
        return Res::Err("Invalid hash, htlc hash is empty and it must be set!");
    if (submitdfchtlc.timeout == 0)
        return Res::Err("Invalid timeout, must be greater than 0!");

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

std::unique_ptr<CICXOrderView::CICXSubmitDFCHTLCImpl> CICXOrderView::HasICXSubmitDFCHTLCOpen(uint256 const & offertxid)
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

std::unique_ptr<CICXOrderView::CICXSubmitEXTHTLCImpl> CICXOrderView::GetICXSubmitEXTHTLCByCreationTx(const uint256 & txid) const
{
    auto submitexthtlc = ReadBy<ICXSubmitEXTHTLCCreationTx,CICXSubmitEXTHTLCImpl>(txid);
    if (submitexthtlc)
        return MakeUnique<CICXSubmitEXTHTLCImpl>(*submitexthtlc);
    return {};
}

Res CICXOrderView::ICXSubmitEXTHTLC(CICXSubmitEXTHTLCImpl const & submitexthtlc)
{
    //this should not happen, but for sure
    if (GetICXSubmitEXTHTLCByCreationTx(submitexthtlc.creationTx))
        return Res::Err("submitexthtlc with creation tx %s already exists!", submitexthtlc.creationTx.GetHex());
    if (submitexthtlc.amount == 0)
        return Res::Err("Invalid amount, must be greater than 0!");
    if (submitexthtlc.htlcscriptAddress.empty())
        return Res::Err("Invalid htlcscriptAddress, htlcscriptAddress is empty and it must be set!");
    if (submitexthtlc.hash.IsNull())
        return Res::Err("Invalid hash, htlc hash is empty and it must be set!");
    if (!submitexthtlc.ownerPubkey.IsFullyValid())
        return Res::Err("Invalid refundPubkey is not a valid pubkey!");
    if (submitexthtlc.timeout == 0)
        return Res::Err("Invalid timout, must be greater than 0!");

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

std::unique_ptr<CICXOrderView::CICXSubmitEXTHTLCImpl> CICXOrderView::HasICXSubmitEXTHTLCOpen(uint256 const & offertxid)
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

std::unique_ptr<CICXOrderView::CICXClaimDFCHTLCImpl> CICXOrderView::GetICXClaimDFCHTLCByCreationTx(uint256 const & txid) const
{
    auto claimdfchtlc = ReadBy<ICXClaimDFCHTLCCreationTx,CICXClaimDFCHTLCImpl>(txid);
    if (claimdfchtlc)
        return MakeUnique<CICXClaimDFCHTLCImpl>(*claimdfchtlc);
    return {};
}

Res CICXOrderView::ICXClaimDFCHTLC(CICXClaimDFCHTLCImpl const & claimdfchtlc, uint256 const & offertxid, CICXOrderImpl const & order)
{
    //this should not happen, but for sure
    if (GetICXClaimDFCHTLCByCreationTx(claimdfchtlc.creationTx))
        return Res::Err("claimdfchtlc with creation tx %s already exists!", claimdfchtlc.creationTx.GetHex());

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

std::unique_ptr<CICXOrderView::CICXCloseOrderImpl> CICXOrderView::GetICXCloseOrderByCreationTx(uint256 const & txid) const
{
    auto closeorderImpl = ReadBy<ICXCloseOrderCreationTx, CICXCloseOrderImpl>(txid);
    if (closeorderImpl)
        return MakeUnique<CICXCloseOrderImpl>(*closeorderImpl);
    return {};
}

Res CICXOrderView::ICXCloseOrder(CICXCloseOrderImpl const & closeorder)
{
    //this should not happen, but for sure
    if (GetICXCloseOrderByCreationTx(closeorder.creationTx))
        return Res::Err("closeorder with creation tx %s already exists!", closeorder.creationTx.GetHex());

    WriteBy<ICXCloseOrderCreationTx>(closeorder.creationTx, closeorder.orderTx);

    return Res::Ok();
}

std::unique_ptr<CICXOrderView::CICXCloseOfferImpl> CICXOrderView::GetICXCloseOfferByCreationTx(uint256 const & txid) const
{
    auto closeofferImpl = ReadBy<ICXCloseOfferCreationTx, CICXCloseOfferImpl>(txid);
    if (closeofferImpl)
        return MakeUnique<CICXCloseOfferImpl>(*closeofferImpl);
    return {};
}

Res CICXOrderView::ICXCloseOffer(CICXCloseOfferImpl const & closeoffer)
{
    //this should not happen, but for sure
    if (GetICXCloseOrderByCreationTx(closeoffer.creationTx))
        return Res::Err("closeooffer with creation tx %s already exists!", closeoffer.creationTx.GetHex());

    WriteBy<ICXCloseOfferCreationTx>(closeoffer.creationTx, closeoffer.offerTx);

    return Res::Ok();
}

Res CICXOrderView::ICXSetTakerFeePerBTC(CAmount amount)
{
    WriteBy<ICXVariables>('A', amount);

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
