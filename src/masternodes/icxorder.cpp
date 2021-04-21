#include <masternodes/icxorder.h>

/// @attention make sure that it does not overlap with other views !!!
const unsigned char CICXOrderView::ICXOrderCreationTx           ::prefix = '1';
const unsigned char CICXOrderView::ICXOrderKey                  ::prefix = '2';
const unsigned char CICXOrderView::ICXOrderStatus               ::prefix = 'B';
const unsigned char CICXOrderView::ICXMakeOfferCreationTx       ::prefix = '3';
const unsigned char CICXOrderView::ICXMakeOfferKey              ::prefix = '4';
const unsigned char CICXOrderView::ICXSubmitDFCHTLCCreationTx   ::prefix = '5';
const unsigned char CICXOrderView::ICXSubmitDFCHTLCKey          ::prefix = '6';
const unsigned char CICXOrderView::ICXSubmitDFCHTLCStatus       ::prefix = 'C';
const unsigned char CICXOrderView::ICXSubmitEXTHTLCCreationTx   ::prefix = '7';
const unsigned char CICXOrderView::ICXSubmitEXTHTLCKey          ::prefix = '8';
const unsigned char CICXOrderView::ICXClaimDFCHTLCCreationTx    ::prefix = '9';
const unsigned char CICXOrderView::ICXClaimDFCHTLCKey           ::prefix = '0';
const unsigned char CICXOrderView::ICXCloseOrderCreationTx      ::prefix = 'A';

const int CICXOrder::DEFAULT_EXPIRY = 2880;
const int CICXOrder::TYPE_INTERNAL = 1;
const int CICXOrder::TYPE_EXTERNAL = 2;
const int CICXOrder::STATUS_OPEN = 0;
const int CICXOrder::STATUS_CLOSED = 1;
const int CICXOrder::STATUS_EXPIRED = 2;

const int CICXMakeOffer::STATUS_OPEN = 0;

const int CICXSubmitDFCHTLC::DEFAULT_TIMEOUT = 100;
const int CICXSubmitDFCHTLC::STATUS_OPEN = 0;
const int CICXSubmitDFCHTLC::STATUS_CLAIMED = 1;
const int CICXSubmitDFCHTLC::STATUS_REFUNDED = 2;

const int CICXSubmitEXTHTLC::STATUS_OPEN = 0;
const int CICXSubmitEXTHTLC::STATUS_EXPIRED = 1;


std::unique_ptr<CICXOrderView::CICXOrderImpl> CICXOrderView::GetICXOrderByCreationTx(const uint256 & txid) const
{
    auto order = ReadBy<ICXOrderCreationTx,CICXOrderImpl>(txid);
    if (order)
        return MakeUnique<CICXOrderImpl>(*order);
    return (nullptr);
}

ResVal<uint256> CICXOrderView::ICXCreateOrder(const CICXOrderImpl& order)
{
    //this should not happen, but for sure
    if (GetICXOrderByCreationTx(order.creationTx)) {
        return Res::Err("order with creation tx %s already exists!", order.creationTx.GetHex());
    }

    OrderKey key({CICXOrder::STATUS_OPEN,{order.idToken,order.chain}}, order.creationTx);
    WriteBy<ICXOrderCreationTx>(order.creationTx, order);
    WriteBy<ICXOrderKey>(key, 1);
    WriteBy<ICXOrderStatus>(StatusKey(order.creationHeight+order.expiry,order.creationTx),CICXOrder::STATUS_EXPIRED);

    return {order.creationTx, Res::Ok()};
}

ResVal<uint256> CICXOrderView::ICXCloseOrderTx(const CICXOrderImpl& order, const uint8_t status)
{
    OrderKey key({CICXOrder::STATUS_OPEN,{order.idToken,order.chain}},order.creationTx);
    EraseBy<ICXOrderKey>(key);
    key.first.first = status;
    WriteBy<ICXOrderKey>(key, 1);
    WriteBy<ICXOrderCreationTx>(order.creationTx, order);
    
    return {order.creationTx, Res::Ok()};
}

void CICXOrderView::ForEachICXOrder(std::function<bool (OrderKey const &, uint8_t)> callback, StatusAsset const & pair)
{
    OrderKey start(pair, uint256());
    ForEach<ICXOrderKey,OrderKey,uint8_t>([&start,&callback] (OrderKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

void CICXOrderView::ForEachICXOrderExpired(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height)
{
    StatusKey start(height, uint256());
    ForEach<ICXOrderStatus,StatusKey,uint8_t>([&start,&callback] (StatusKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

std::unique_ptr<CICXOrderView::CICXMakeOfferImpl> CICXOrderView::GetICXMakeOfferByCreationTx(const uint256 & txid) const
{
    auto makeoffer = ReadBy<ICXMakeOfferCreationTx,CICXMakeOfferImpl>(txid);
    if (makeoffer)
        return MakeUnique<CICXMakeOfferImpl>(*makeoffer);
    return (nullptr);
}

ResVal<uint256> CICXOrderView::ICXMakeOffer(const CICXMakeOfferImpl& makeoffer, const CICXOrderImpl & order)
{
    //this should not happen, but for sure
    if (GetICXMakeOfferByCreationTx(makeoffer.creationTx)) {
        return Res::Err("makeoffer with creation tx %s already exists!", makeoffer.creationTx.GetHex());
    }

    WriteBy<ICXMakeOfferCreationTx>(makeoffer.creationTx, makeoffer);
    WriteBy<ICXMakeOfferKey>(std::make_pair(order.creationTx,makeoffer.creationTx), 1);

    if (order.closeHeight > -1) this->ICXCloseOrderTx(order,CICXOrder::STATUS_CLOSED);
    else
    {
        OrderKey key({CICXOrder::STATUS_OPEN, {order.idToken,order.chain}}, order.creationTx);
        WriteBy<ICXOrderKey>(key, 1);
        WriteBy<ICXOrderCreationTx>(order.creationTx, order);
    }

    return {makeoffer.creationTx, Res::Ok()};
}

void CICXOrderView::ForEachICXMakeOffer(std::function<bool (TxidPairKey const &, uint8_t)> callback, uint256 const & txid)
{
    TxidPairKey start(txid,uint256());
    ForEach<ICXMakeOfferKey,TxidPairKey,uint8_t>([&callback] (TxidPairKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

std::unique_ptr<CICXOrderView::CICXSubmitDFCHTLCImpl> CICXOrderView::GetICXSubmitDFCHTLCByCreationTx(const uint256 & txid) const
{
    auto submitdfchtlc = ReadBy<ICXSubmitDFCHTLCCreationTx,CICXSubmitDFCHTLCImpl>(txid);
    if (submitdfchtlc)
        return MakeUnique<CICXSubmitDFCHTLCImpl>(*submitdfchtlc);
    return (nullptr);
}

ResVal<uint256> CICXOrderView::ICXSubmitDFCHTLC(const CICXSubmitDFCHTLCImpl& submitdfchtlc)
{
    //this should not happen, but for sure
    if (GetICXSubmitDFCHTLCByCreationTx(submitdfchtlc.creationTx)) {
        return Res::Err("submitdfchtlc with creation tx %s already exists!", submitdfchtlc.creationTx.GetHex());
    }

    WriteBy<ICXSubmitDFCHTLCCreationTx>(submitdfchtlc.creationTx, submitdfchtlc);
    WriteBy<ICXSubmitDFCHTLCKey>(StatusTxidKey({CICXSubmitDFCHTLC::STATUS_OPEN,submitdfchtlc.offerTx},submitdfchtlc.creationTx), 1);
    WriteBy<ICXSubmitDFCHTLCStatus>(StatusKey(submitdfchtlc.creationHeight+submitdfchtlc.timeout,submitdfchtlc.creationTx),CICXSubmitDFCHTLC::STATUS_REFUNDED);

    return {submitdfchtlc.creationTx, Res::Ok()};
}

Res CICXOrderView::ICXRefundDFCHTLC(CICXSubmitDFCHTLCImpl& submitdfchtlc)
{
    WriteBy<ICXSubmitDFCHTLCCreationTx>(submitdfchtlc.creationTx, submitdfchtlc);
    EraseBy<ICXSubmitDFCHTLCKey>(StatusTxidKey({CICXSubmitDFCHTLC::STATUS_OPEN,submitdfchtlc.offerTx},submitdfchtlc.creationTx));
    WriteBy<ICXSubmitDFCHTLCKey>(StatusTxidKey({CICXSubmitDFCHTLC::STATUS_REFUNDED,submitdfchtlc.offerTx},submitdfchtlc.creationTx),1);

    return (Res::Ok());
}

void CICXOrderView::ForEachICXSubmitDFCHTLC(std::function<bool (StatusTxidKey const &, uint8_t)> callback, StatusTxid const & statustxid)
{
    StatusTxidKey start(statustxid,uint256());
    ForEach<ICXSubmitDFCHTLCKey,StatusTxidKey,uint8_t>([&callback] (StatusTxidKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

void CICXOrderView::ForEachICXSubmitDFCHTLCExpired(std::function<bool (StatusKey const &, uint8_t)> callback, uint32_t const & height)
{
    StatusKey start(height,uint256());
    ForEach<ICXSubmitDFCHTLCStatus,StatusKey,uint8_t>([&callback] (StatusKey const &key, uint8_t i) {
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

ResVal<uint256> CICXOrderView::ICXSubmitEXTHTLC(const CICXSubmitEXTHTLCImpl& submitexthtlc)
{
    //this should not happen, but for sure
    if (GetICXSubmitEXTHTLCByCreationTx(submitexthtlc.creationTx)) {
        return Res::Err("submitexthtlc with creation tx %s already exists!", submitexthtlc.creationTx.GetHex());
    }

    WriteBy<ICXSubmitEXTHTLCCreationTx>(submitexthtlc.creationTx, submitexthtlc);
    WriteBy<ICXSubmitEXTHTLCKey>(StatusTxidKey({CICXSubmitEXTHTLC::STATUS_OPEN,submitexthtlc.offerTx}, submitexthtlc.creationTx),1);
    
    return {submitexthtlc.creationTx, Res::Ok()};
}

void CICXOrderView::ForEachICXSubmitEXTHTLC(std::function<bool (StatusTxidKey const &, uint8_t)> callback, StatusTxid const & statustxid)
{
    StatusTxidKey start(statustxid,uint256());
    ForEach<ICXSubmitEXTHTLCKey,StatusTxidKey,uint8_t>([&callback] (StatusTxidKey const &key, uint8_t i) {
        return callback(key, i);
    }, start);
}

std::unique_ptr<CICXOrderView::CICXClaimDFCHTLCImpl> CICXOrderView::GetICXClaimDFCHTLCByCreationTx(const uint256 & txid) const
{
    auto claimdfchtlc = ReadBy<ICXClaimDFCHTLCCreationTx,CICXClaimDFCHTLCImpl>(txid);
    if (claimdfchtlc)
        return MakeUnique<CICXClaimDFCHTLCImpl>(*claimdfchtlc);
    return (nullptr);
}

ResVal<uint256> CICXOrderView::ICXClaimDFCHTLC(const CICXClaimDFCHTLCImpl& claimdfchtlc, const CICXSubmitDFCHTLCImpl& dfchtlc)
{
    //this should not happen, but for sure
    if (GetICXClaimDFCHTLCByCreationTx(claimdfchtlc.creationTx)) {
        return Res::Err("claimdfchtlc with creation tx %s already exists!", claimdfchtlc.creationTx.GetHex());
    }

    WriteBy<ICXClaimDFCHTLCCreationTx>(claimdfchtlc.creationTx, claimdfchtlc);
    
    EraseBy<ICXSubmitDFCHTLCKey>(StatusTxidKey({CICXSubmitDFCHTLC::STATUS_OPEN,dfchtlc.offerTx},dfchtlc.creationTx));
    WriteBy<ICXSubmitDFCHTLCKey>(StatusTxidKey({CICXSubmitDFCHTLC::STATUS_CLAIMED,dfchtlc.offerTx},dfchtlc.creationTx),1);
    
    return {claimdfchtlc.creationTx, Res::Ok()};
}

void CICXOrderView::ForEachICXClaimDFCHTLC(std::function<bool (TxidPairKey const &, CLazySerialize<CICXClaimDFCHTLCImpl>)> callback, uint256 const & txid)
{
    TxidPairKey start(txid,uint256());
    ForEach<ICXClaimDFCHTLCCreationTx,TxidPairKey,CICXClaimDFCHTLCImpl>([&callback] (TxidPairKey const &key, CLazySerialize<CICXClaimDFCHTLCImpl> claimdfchtlc) {
        return callback(key, claimdfchtlc);
    }, start);
}

std::unique_ptr<CICXOrderView::CICXCloseOrderImpl> CICXOrderView::GetICXCloseOrderByCreationTx(const uint256 & txid) const
{
    auto closeorderImpl = ReadBy<ICXCloseOrderCreationTx, CICXCloseOrderImpl>(txid);
    if (closeorderImpl)
        return MakeUnique<CICXCloseOrderImpl>(*closeorderImpl);
    return (nullptr);
}

ResVal<uint256> CICXOrderView::ICXCloseOrder(const CICXCloseOrderImpl& closeorder)
{
    //this should not happen, but for sure
    if (GetICXCloseOrderByCreationTx(closeorder.creationTx)) {
        return Res::Err("closeorder with creation tx %s already exists!", closeorder.creationTx.GetHex());
    }
    
    WriteBy<ICXCloseOrderCreationTx>(closeorder.creationTx, closeorder.orderTx);

    return {closeorder.creationTx, Res::Ok()};
}
