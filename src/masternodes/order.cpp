#include <masternodes/order.h>
// #include <masternodes/masternodes.h>

/// @attention make sure that it does not overlap with other views !!!
const unsigned char COrderView::OrderCreationTx  ::prefix = 'O';
const unsigned char COrderView::OrderCreationTxId  ::prefix = 'P';
const unsigned char COrderView::FulfillCreationTx  ::prefix = 'D';
const unsigned char COrderView::FulfillOrderTxid  ::prefix = 'Q';
const unsigned char COrderView::CloseCreationTx  ::prefix = 'E';
const unsigned char COrderView::OrderCloseTx  ::prefix = 'C';

std::unique_ptr<COrderView::COrderImpl> COrderView::GetOrderByCreationTx(const uint256 & txid) const
{
    auto tokenPair=ReadBy<OrderCreationTxId, TokenPair>(txid);
    if (tokenPair)
    {
        auto orderImpl=ReadBy<OrderCreationTx,COrderImpl>(std::make_pair(*tokenPair,txid));
        if (orderImpl)
            return MakeUnique<COrderImpl>(*orderImpl);
    }
    return nullptr;
}

ResVal<uint256> COrderView::CreateOrder(const COrderView::COrderImpl& order)
{
    //this should not happen, but for sure
    if (GetOrderByCreationTx(order.creationTx)) {
        return Res::Err("order with creation tx %s already exists!", order.creationTx.GetHex());
    }
    TokenPair pair(order.idTokenFrom,order.idTokenTo);
    TokenPairKey key(pair,order.creationTx);
    WriteBy<OrderCreationTx>(key,order);
    WriteBy<OrderCreationTxId>(order.creationTx,pair);

    return {order.creationTx, Res::Ok()};
}

ResVal<uint256> COrderView::CloseOrderTx(const uint256& txid)
{
    auto order=GetOrderByCreationTx(txid);
    if (!order) {
        return Res::Err("order with creation tx %s doesn't exists!", txid.GetHex());
    }

    TokenPair pair(order->idTokenFrom,order->idTokenTo);
    TokenPairKey key(pair,order->creationTx);
    EraseBy<OrderCreationTx>(key);
    WriteBy<OrderCreationTx>(key,*order);
    WriteBy<OrderCloseTx>(order->closeTx, order->creationTx);
    return {order->creationTx, Res::Ok()};
}

void COrderView::ForEachOrder(std::function<bool (COrderView::TokenPairKey const &, CLazySerialize<COrderView::COrderImpl>)> callback, TokenPair const & pair)
{
    TokenPairKey start(pair,uint256());
    ForEachOrder<OrderCreationTx,TokenPairKey,COrderImpl>([&callback] (TokenPairKey const &key, CLazySerialize<COrderImpl> orderImpl) {
        return callback(key, orderImpl);
    }, start);
}

std::unique_ptr<COrderView::CFulfillOrderImpl> COrderView::GetFulfillOrderByCreationTx(const uint256 & txid) const
{
    auto ordertxid=ReadBy<FulfillOrderTxid, uint256>(txid);
    if (!ordertxid->IsNull())
    {
        auto fillorderImpl=ReadBy<FulfillCreationTx,CFulfillOrderImpl>(std::make_pair(*ordertxid,txid));
        if (fillorderImpl)
            return MakeUnique<CFulfillOrderImpl>(*fillorderImpl);
        return nullptr;
    }
}

ResVal<uint256> COrderView::FulfillOrder(const COrderView::CFulfillOrderImpl & fillorder)
{
    //this should not happen, but for sure
    if (GetFulfillOrderByCreationTx(fillorder.creationTx)) {
        return Res::Err("fillorder with creation tx %s already exists!", fillorder.creationTx.GetHex());
    }

    WriteBy<FulfillOrderTxid>(fillorder.creationTx, fillorder.orderTx);
    WriteBy<FulfillCreationTx>(std::make_pair(fillorder.orderTx,fillorder.creationTx), fillorder);
    return {fillorder.creationTx, Res::Ok()};
}

std::unique_ptr<COrderView::CCloseOrderImpl> COrderView::GetCloseOrderByCreationTx(const uint256 & txid) const
{
    auto closeorderImpl=ReadBy<CloseCreationTx, CCloseOrderImpl>(txid);
    if (closeorderImpl)
        return MakeUnique<CCloseOrderImpl>(*closeorderImpl);
    return nullptr;
}

ResVal<uint256> COrderView::CloseOrder(const COrderView::CCloseOrderImpl& closeorder)
{
    if (GetCloseOrderByCreationTx(closeorder.creationTx)) {
        return Res::Err("close with creation tx %s already exists!", closeorder.creationTx.GetHex());
    }
    if (!CloseOrderTx(closeorder.orderTx).ok) {
        return Res::Err("order with creation tx %s doesn't exists!", closeorder.orderTx.GetHex());
    }
    WriteBy<CloseCreationTx>(closeorder.creationTx, closeorder);

    return {closeorder.creationTx, Res::Ok()};
}
