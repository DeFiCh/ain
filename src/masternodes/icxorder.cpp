#include <masternodes/icxorder.h>

/// @attention make sure that it does not overlap with other views !!!
const unsigned char CICXOrderView::ICXOrderCreationTx  ::prefix = '1';
const unsigned char CICXOrderView::ICXOrderCreationTxid  ::prefix = '7';
const unsigned char CICXOrderView::ICXMakeOfferCreationTx  ::prefix = '2';
const unsigned char CICXOrderView::ICXSubmitDFCHTLCCreationTx  ::prefix = '3';
const unsigned char CICXOrderView::ICXSubmitEXTHTLCCreationTx  ::prefix = '4';
const unsigned char CICXOrderView::ICXClaimDFCHTLCCreationTx  ::prefix = '5';
const unsigned char CICXOrderView::ICXCloseOrderCreationTx  ::prefix = '6';

std::unique_ptr<CICXOrderView::CICXOrderImpl> CICXOrderView::GetICXOrderByCreationTx(const uint256 & txid) const
{
    auto assetPair = ReadBy<ICXOrderCreationTxid, AssetPair>(txid);
    if (assetPair)
    {
        auto order = ReadBy<ICXOrderCreationTx,CICXOrderImpl>(std::make_pair(*assetPair, txid));
        if (order)
            return MakeUnique<CICXOrderImpl>(*order);
    }
    return (nullptr);
}

ResVal<uint256> CICXOrderView::ICXCreateOrder(const CICXOrderImpl& order)
{
    //this should not happen, but for sure
    if (GetICXOrderByCreationTx(order.creationTx)) {
        return Res::Err("order with creation tx %s already exists!", order.creationTx.GetHex());
    }

    AssetPair pair;
    if (order.orderType) pair={order.idTokenFrom,order.chainTo};
    else pair={order.idTokenTo,order.chainFrom};

    AssetPairKey key(pair, order.creationTx);
    WriteBy<ICXOrderCreationTx>(key, order);
    WriteBy<ICXOrderCreationTxid>(order.creationTx, pair);

    return {order.creationTx, Res::Ok()};
}

ResVal<uint256> CICXOrderView::ICXCloseOrderTx(const CICXOrderImpl& order)
{
    AssetPairKey key;
    if (order.chainFrom.empty()) key={{order.idTokenFrom,order.chainTo},order.creationTx};
    else key={{order.idTokenTo,order.chainFrom},order.creationTx};
    
    EraseBy<ICXOrderCreationTx>(key);
    WriteBy<ICXCloseOrderCreationTx>(key, order);
    
    return {order.creationTx, Res::Ok()};
}

void CICXOrderView::ForEachICXOrder(std::function<bool (AssetPairKey const &, CLazySerialize<CICXOrderImpl>)> callback, AssetPair const & pair)
{
    AssetPairKey start(pair, uint256());
    ForEach<ICXOrderCreationTx,AssetPairKey,CICXOrderImpl>([&start,&callback] (AssetPairKey const &key, CLazySerialize<CICXOrderImpl> order) {
        return callback(key, order);
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

    if (order.closeHeight > -1) this->ICXCloseOrderTx(order);
    else
    {
        AssetPair pair;
        if (order.orderType) pair={order.idTokenFrom,order.chainTo};
        else pair={order.idTokenTo,order.chainFrom};
        AssetPairKey key(pair, order.creationTx);
        WriteBy<ICXOrderCreationTx>(key, order);
    }

    return {makeoffer.creationTx, Res::Ok()};
}

void CICXOrderView::ForEachICXMakeOffer(std::function<bool (uint256 const &, CLazySerialize<CICXMakeOfferImpl>)> callback, uint256 const & txid)
{
    ForEach<ICXMakeOfferCreationTx,uint256,CICXMakeOfferImpl>([&callback] (uint256 const &key, CLazySerialize<CICXMakeOfferImpl> makeoffer) {
        return callback(key, makeoffer);
    }, txid);
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
    if (GetICXOrderByCreationTx(submitdfchtlc.creationTx)) {
        return Res::Err("submitdfchtlc with creation tx %s already exists!", submitdfchtlc.creationTx.GetHex());
    }

    WriteBy<ICXSubmitDFCHTLCCreationTx>(submitdfchtlc.creationTx, submitdfchtlc);

    return {submitdfchtlc.creationTx, Res::Ok()};
}

void CICXOrderView::ForEachICXSubmitDFCHTLC(std::function<bool (uint256 const &, CLazySerialize<CICXSubmitDFCHTLCImpl>)> callback, uint256 const & txid)
{
    ForEach<ICXSubmitDFCHTLCCreationTx,uint256,CICXSubmitDFCHTLCImpl>([&callback] (uint256 const &key, CLazySerialize<CICXSubmitDFCHTLCImpl> submitdfchtlc) {
        return callback(key, submitdfchtlc);
    }, txid);
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
    if (GetICXOrderByCreationTx(submitexthtlc.creationTx)) {
        return Res::Err("submitexthtlc with creation tx %s already exists!", submitexthtlc.creationTx.GetHex());
    }

    WriteBy<ICXSubmitEXTHTLCCreationTx>(submitexthtlc.creationTx, submitexthtlc);

    return {submitexthtlc.creationTx, Res::Ok()};
}

void CICXOrderView::ForEachICXSubmitEXTHTLC(std::function<bool (uint256 const &, CLazySerialize<CICXSubmitEXTHTLCImpl>)> callback, uint256 const & txid)
{
    ForEach<ICXSubmitEXTHTLCCreationTx,uint256,CICXSubmitEXTHTLCImpl>([&callback] (uint256 const &key, CLazySerialize<CICXSubmitEXTHTLCImpl> submitexthtlc) {
        return callback(key, submitexthtlc);
    }, txid);
}

std::unique_ptr<CICXOrderView::CICXClaimDFCHTLCImpl> CICXOrderView::GetICXClaimDFCHTLCByCreationTx(const uint256 & txid) const
{
    auto claimdfchtlc = ReadBy<ICXClaimDFCHTLCCreationTx,CICXClaimDFCHTLCImpl>(txid);
    if (claimdfchtlc)
        return MakeUnique<CICXClaimDFCHTLCImpl>(*claimdfchtlc);
    return (nullptr);

}

ResVal<uint256> CICXOrderView::ICXClaimDFCHTLC(const CICXClaimDFCHTLCImpl& claimdfchtlc)
{
    //this should not happen, but for sure
    if (GetICXOrderByCreationTx(claimdfchtlc.creationTx)) {
        return Res::Err("claimdfchtlc with creation tx %s already exists!", claimdfchtlc.creationTx.GetHex());
    }

    WriteBy<ICXClaimDFCHTLCCreationTx>(claimdfchtlc.creationTx, claimdfchtlc);

    return {claimdfchtlc.creationTx, Res::Ok()};
}

void CICXOrderView::ForEachICXClaimDFCHTLC(std::function<bool (uint256 const &, CLazySerialize<CICXClaimDFCHTLCImpl>)> callback, uint256 const & txid)
{
    ForEach<ICXClaimDFCHTLCCreationTx,uint256,CICXClaimDFCHTLCImpl>([&callback] (uint256 const &key, CLazySerialize<CICXClaimDFCHTLCImpl> claimdfchtlc) {
        return callback(key, claimdfchtlc);
    }, txid);
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
    WriteBy<ICXCloseOrderCreationTx>(closeorder.creationTx, closeorder);

    return {closeorder.creationTx, Res::Ok()};
}

void CICXOrderView::ForEachICXClosedOrder(std::function<bool (AssetPairKey const &, CLazySerialize<CICXOrderImpl>)> callback, AssetPair const & pair)
{
    AssetPairKey start(pair,uint256());
    ForEach<ICXCloseOrderCreationTx,AssetPairKey,CICXOrderImpl>([&start,&callback] (AssetPairKey const &key, CLazySerialize<CICXOrderImpl> orderImpl) {
        return callback(key, orderImpl);
    }, start);
}
