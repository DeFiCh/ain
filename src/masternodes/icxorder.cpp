#include <masternodes/icxorder.h>

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
const unsigned char CICXOrderView::ICXSubmitEXTHTLCKey          ::prefix = 0x07;
const unsigned char CICXOrderView::ICXClaimDFCHTLCKey           ::prefix = 0x08;

const unsigned char CICXOrderView::ICXOrderStatus               ::prefix = 0x09;
const unsigned char CICXOrderView::ICXOfferStatus               ::prefix = 0x0A;
const unsigned char CICXOrderView::ICXSubmitDFCHTLCStatus       ::prefix = 0x0B;

const uint32_t CICXOrder::DEFAULT_EXPIRY = 2880;
const uint8_t CICXOrder::TYPE_INTERNAL = 1;
const uint8_t CICXOrder::TYPE_EXTERNAL = 2;
const uint8_t CICXOrder::STATUS_OPEN = 0;
const uint8_t CICXOrder::STATUS_CLOSED = 1;
const uint8_t CICXOrder::STATUS_FILLED = 2;
const uint8_t CICXOrder::STATUS_EXPIRED = 3;
const uint8_t CICXOrder::DFI_TOKEN_ID = 0;
const std::string CICXOrder::CHAIN_BTC = "BTC";

const uint32_t CICXMakeOffer::DEFAULT_EXPIRY = 100;
const uint8_t CICXMakeOffer::STATUS_OPEN = 0;
const uint8_t CICXMakeOffer::STATUS_CLOSED = 1;
const uint8_t CICXMakeOffer::STATUS_EXPIRED = 2;
const int64_t CICXMakeOffer::TAKER_FEE_PER_BTC = 0.1;

const uint32_t CICXSubmitDFCHTLC::DEFAULT_TIMEOUT = 100;
const uint8_t CICXSubmitDFCHTLC::STATUS_OPEN = 0;
const uint8_t CICXSubmitDFCHTLC::STATUS_CLAIMED = 1;
const uint8_t CICXSubmitDFCHTLC::STATUS_REFUNDED = 2;

const uint8_t CICXSubmitEXTHTLC::STATUS_OPEN = 0;

std::unique_ptr<CICXOrderView::CICXOrderImpl> CICXOrderView::GetICXOrderByCreationTx(uint256 const & txid) const
{
    auto order = ReadBy<ICXOrderCreationTx,CICXOrderImpl>(txid);
    if (order)
        return MakeUnique<CICXOrderImpl>(*order);
    return (nullptr);
}

ResVal<uint256> CICXOrderView::ICXCreateOrder(CICXOrderImpl const & order)
{
    //this should not happen, but for sure
    if (GetICXOrderByCreationTx(order.creationTx)) {
        return Res::Err("order with creation tx %s already exists!", order.creationTx.GetHex());
    }

    OrderKey key({order.idToken,order.chain}, order.creationTx);
    WriteBy<ICXOrderCreationTx>(order.creationTx, order);
    WriteBy<ICXOrderOpenKey>(key, CICXOrder::STATUS_OPEN);
    WriteBy<ICXOrderStatus>(StatusKey(order.creationHeight + order.expiry, order.creationTx), CICXOrder::STATUS_EXPIRED);

    return {order.creationTx, Res::Ok()};
}

Res CICXOrderView::ICXUpdateOrder(CICXOrderImpl const & order)
{
    //this should not happen, but for sure
    if (!GetICXOrderByCreationTx(order.creationTx)) {
        return Res::Err("order with creation tx %s doesn't exists!", order.creationTx.GetHex());
    }

    OrderKey key({order.idToken,order.chain}, order.creationTx);
    WriteBy<ICXOrderCreationTx>(order.creationTx, order);

    return (Res::Ok());
}

Res CICXOrderView::ICXCloseOrderTx(CICXOrderImpl const & order, uint8_t const status)
{
    WriteBy<ICXOrderCreationTx>(order.creationTx, order);
    OrderKey key({order.idToken,order.chain}, order.creationTx);
    EraseBy<ICXOrderOpenKey>(key);
    WriteBy<ICXOrderCloseKey>(key, status);
    
    return (Res::Ok());
}

void CICXOrderView::ForEachICXOrderOpen(std::function<bool (OrderKey const &, uint8_t)> callback, AssetPair const & pair)
{
    OrderKey start(pair, uint256());
    ForEach<ICXOrderOpenKey,OrderKey,uint8_t>([&start, &callback] (OrderKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

void CICXOrderView::ForEachICXOrderClose(std::function<bool (OrderKey const &, uint8_t)> callback, AssetPair const & pair)
{
    OrderKey start(pair, uint256());
    ForEach<ICXOrderCloseKey,OrderKey,uint8_t>([&start, &callback] (OrderKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

void CICXOrderView::ForEachICXOrderExpire(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height)
{
    StatusKey start(height, uint256());
    ForEach<ICXOrderStatus,StatusKey,uint8_t>([&start, &callback] (StatusKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

std::unique_ptr<CICXOrderView::CICXMakeOfferImpl> CICXOrderView::GetICXMakeOfferByCreationTx(uint256 const & txid) const
{
    auto makeoffer = ReadBy<ICXMakeOfferCreationTx,CICXMakeOfferImpl>(txid);
    if (makeoffer)
        return MakeUnique<CICXMakeOfferImpl>(*makeoffer);
    return (nullptr);
}

ResVal<uint256> CICXOrderView::ICXMakeOffer(CICXMakeOfferImpl const & makeoffer)
{
    //this should not happen, but for sure
    if (GetICXMakeOfferByCreationTx(makeoffer.creationTx)) {
        return Res::Err("makeoffer with creation tx %s already exists!", makeoffer.creationTx.GetHex());
    }

    WriteBy<ICXMakeOfferCreationTx>(makeoffer.creationTx, makeoffer);
    WriteBy<ICXMakeOfferOpenKey>(TxidPairKey(makeoffer.orderTx, makeoffer.creationTx), CICXMakeOffer::STATUS_OPEN);
    WriteBy<ICXOfferStatus>(StatusKey(makeoffer.creationHeight + makeoffer.expiry, makeoffer.creationTx), CICXMakeOffer::STATUS_EXPIRED);

    return {makeoffer.creationTx, Res::Ok()};
}

Res CICXOrderView::ICXCloseMakeOfferTx(CICXMakeOfferImpl const & makeoffer, uint8_t const status)
{
    TxidPairKey key(makeoffer.orderTx,makeoffer.creationTx);
    EraseBy<ICXMakeOfferOpenKey>(key);
    WriteBy<ICXMakeOfferCloseKey>(key, status);
    
    return (Res::Ok());
}

void CICXOrderView::ForEachICXMakeOfferOpen(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & txid)
{
    TxidPairKey start(txid, uint256());
    ForEach<ICXMakeOfferOpenKey,TxidPairKey,uint8_t>([&callback] (TxidPairKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

void CICXOrderView::ForEachICXMakeOfferClose(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & txid)
{
    TxidPairKey start(txid, uint256());
    ForEach<ICXMakeOfferCloseKey,TxidPairKey,uint8_t>([&callback] (TxidPairKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

void CICXOrderView::ForEachICXMakeOfferExpire(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height)
{
    StatusKey start(height, uint256());
    ForEach<ICXOfferStatus,StatusKey,uint8_t>([&start, &callback] (StatusKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

std::unique_ptr<CICXOrderView::CICXSubmitDFCHTLCImpl> CICXOrderView::GetICXSubmitDFCHTLCByCreationTx(uint256 const & txid) const
{
    auto submitdfchtlc = ReadBy<ICXSubmitDFCHTLCCreationTx,CICXSubmitDFCHTLCImpl>(txid);
    if (submitdfchtlc)
        return MakeUnique<CICXSubmitDFCHTLCImpl>(*submitdfchtlc);
    return (nullptr);
}

ResVal<uint256> CICXOrderView::ICXSubmitDFCHTLC(CICXSubmitDFCHTLCImpl const & submitdfchtlc)
{
    //this should not happen, but for sure
    if (GetICXSubmitDFCHTLCByCreationTx(submitdfchtlc.creationTx)) {
        return Res::Err("submitdfchtlc with creation tx %s already exists!", submitdfchtlc.creationTx.GetHex());
    }

    WriteBy<ICXSubmitDFCHTLCCreationTx>(submitdfchtlc.creationTx, submitdfchtlc);
    WriteBy<ICXSubmitDFCHTLCOpenKey>(TxidPairKey(submitdfchtlc.offerTx, submitdfchtlc.creationTx), CICXSubmitDFCHTLC::STATUS_OPEN);
    WriteBy<ICXSubmitDFCHTLCStatus>(StatusKey(submitdfchtlc.creationHeight + submitdfchtlc.timeout,submitdfchtlc.creationTx),CICXSubmitDFCHTLC::STATUS_REFUNDED);

    return {submitdfchtlc.creationTx, Res::Ok()};
}

Res CICXOrderView::ICXRefundDFCHTLC(CICXSubmitDFCHTLCImpl const & submitdfchtlc)
{
    WriteBy<ICXSubmitDFCHTLCCreationTx>(submitdfchtlc.creationTx, submitdfchtlc);
    EraseBy<ICXSubmitDFCHTLCOpenKey>(TxidPairKey(submitdfchtlc.offerTx, submitdfchtlc.creationTx));
    WriteBy<ICXSubmitDFCHTLCCloseKey>(TxidPairKey(submitdfchtlc.offerTx, submitdfchtlc.creationTx), CICXSubmitDFCHTLC::STATUS_REFUNDED);

    return (Res::Ok());
}

void CICXOrderView::ForEachICXSubmitDFCHTLCOpen(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid)
{
    TxidPairKey start(offertxid, uint256());
    ForEach<ICXSubmitDFCHTLCOpenKey, TxidPairKey, uint8_t>([&callback] (TxidPairKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

void CICXOrderView::ForEachICXSubmitDFCHTLCClose(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid)
{
    TxidPairKey start(offertxid,uint256());
    ForEach<ICXSubmitDFCHTLCCloseKey, TxidPairKey, uint8_t>([&callback] (TxidPairKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

void CICXOrderView::ForEachICXSubmitDFCHTLCExpire(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height)
{
    StatusKey start(height, uint256());
    ForEach<ICXSubmitDFCHTLCStatus, StatusKey, uint8_t>([&callback] (StatusKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

std::unique_ptr<CICXOrderView::CICXSubmitEXTHTLCImpl> CICXOrderView::GetICXSubmitEXTHTLCByCreationTx(const uint256 & txid) const
{
    auto submitexthtlc = ReadBy<ICXSubmitEXTHTLCCreationTx,CICXSubmitEXTHTLCImpl>(txid);
    if (submitexthtlc)
        return MakeUnique<CICXSubmitEXTHTLCImpl>(*submitexthtlc);
    return (nullptr);
}

ResVal<uint256> CICXOrderView::ICXSubmitEXTHTLC(CICXSubmitEXTHTLCImpl const & submitexthtlc)
{
    //this should not happen, but for sure
    if (GetICXSubmitEXTHTLCByCreationTx(submitexthtlc.creationTx)) {
        return Res::Err("submitexthtlc with creation tx %s already exists!", submitexthtlc.creationTx.GetHex());
    }

    WriteBy<ICXSubmitEXTHTLCCreationTx>(submitexthtlc.creationTx, submitexthtlc);
    WriteBy<ICXSubmitEXTHTLCKey>(TxidPairKey(submitexthtlc.offerTx, submitexthtlc.creationTx), CICXSubmitEXTHTLC::STATUS_OPEN);
    
    return {submitexthtlc.creationTx, Res::Ok()};
}

void CICXOrderView::ForEachICXSubmitEXTHTLC(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid)
{
    TxidPairKey start(offertxid, uint256());
    ForEach<ICXSubmitEXTHTLCKey,TxidPairKey,uint8_t>([&callback] (TxidPairKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

std::unique_ptr<CICXOrderView::CICXClaimDFCHTLCImpl> CICXOrderView::GetICXClaimDFCHTLCByCreationTx(uint256 const & txid) const
{
    auto claimdfchtlc = ReadBy<ICXClaimDFCHTLCCreationTx,CICXClaimDFCHTLCImpl>(txid);
    if (claimdfchtlc)
        return MakeUnique<CICXClaimDFCHTLCImpl>(*claimdfchtlc);
    return (nullptr);
}

ResVal<uint256> CICXOrderView::ICXClaimDFCHTLC(CICXClaimDFCHTLCImpl const & claimdfchtlc, CICXOrderImpl const & order)
{
    //this should not happen, but for sure
    if (GetICXClaimDFCHTLCByCreationTx(claimdfchtlc.creationTx)) {
        return Res::Err("claimdfchtlc with creation tx %s already exists!", claimdfchtlc.creationTx.GetHex());
    }

    auto dfchtlc = GetICXSubmitDFCHTLCByCreationTx(claimdfchtlc.dfchtlcTx);
    if (!dfchtlc) {
        return Res::Err("submitdfchtlc tx %s cannot be found!", claimdfchtlc.dfchtlcTx.GetHex());
    }

    std::cout << dfchtlc->offerTx.GetHex() << "|" << claimdfchtlc.creationTx.GetHex() << std::endl;
    WriteBy<ICXClaimDFCHTLCCreationTx>(claimdfchtlc.creationTx, claimdfchtlc);
    WriteBy<ICXClaimDFCHTLCKey>(TxidPairKey(dfchtlc->offerTx, claimdfchtlc.creationTx),CICXSubmitDFCHTLC::STATUS_CLAIMED);
    
    EraseBy<ICXSubmitDFCHTLCOpenKey>(TxidPairKey(dfchtlc->offerTx, dfchtlc->creationTx));
    WriteBy<ICXSubmitDFCHTLCCloseKey>(TxidPairKey(dfchtlc->offerTx, dfchtlc->creationTx), CICXSubmitDFCHTLC::STATUS_CLAIMED);

    if (order.amountToFill != 0) 
    {
        OrderKey key({order.idToken,order.chain}, order.creationTx);
        WriteBy<ICXOrderCreationTx>(order.creationTx, order);
    }
    
    return {claimdfchtlc.creationTx, Res::Ok()};
}

void CICXOrderView::ForEachICXClaimDFCHTLC(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & offertxid)
{
    TxidPairKey start(offertxid, uint256());
    ForEach<ICXClaimDFCHTLCKey,TxidPairKey,uint8_t>([&callback] (TxidPairKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

std::unique_ptr<CICXOrderView::CICXCloseOrderImpl> CICXOrderView::GetICXCloseOrderByCreationTx(uint256 const & txid) const
{
    auto closeorderImpl = ReadBy<ICXCloseOrderCreationTx, CICXCloseOrderImpl>(txid);
    if (closeorderImpl)
        return MakeUnique<CICXCloseOrderImpl>(*closeorderImpl);
    return (nullptr);
}

ResVal<uint256> CICXOrderView::ICXCloseOrder(CICXCloseOrderImpl const & closeorder)
{
    //this should not happen, but for sure
    if (GetICXCloseOrderByCreationTx(closeorder.creationTx)) {
        return Res::Err("closeorder with creation tx %s already exists!", closeorder.creationTx.GetHex());
    }
    
    WriteBy<ICXCloseOrderCreationTx>(closeorder.creationTx, closeorder.orderTx);

    return {closeorder.creationTx, Res::Ok()};
}

std::unique_ptr<CICXOrderView::CICXCloseOfferImpl> CICXOrderView::GetICXCloseOfferByCreationTx(uint256 const & txid) const
{
    auto closeofferImpl = ReadBy<ICXCloseOfferCreationTx, CICXCloseOfferImpl>(txid);
    if (closeofferImpl)
        return MakeUnique<CICXCloseOfferImpl>(*closeofferImpl);
    return (nullptr);
}

ResVal<uint256> CICXOrderView::ICXCloseOffer(CICXCloseOfferImpl const & closeoffer)
{
    //this should not happen, but for sure
    if (GetICXCloseOrderByCreationTx(closeoffer.creationTx)) {
        return Res::Err("closeooffer with creation tx %s already exists!", closeoffer.creationTx.GetHex());
    }
    
    WriteBy<ICXCloseOfferCreationTx>(closeoffer.creationTx, closeoffer.offerTx);

    return {closeoffer.creationTx, Res::Ok()};
}
