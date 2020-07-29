// Copyright (c) 2019 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/orders.h>

/// @attention make sure that it does not overlap with those in masternodes.cpp/tokens.cpp/undos.cpp/accounts.cpp !!!
const unsigned char COrdersView::ByCreationTx::prefix = 'R';
const unsigned char COrdersView::ByExpiryHeight::prefix = 'e';

boost::optional<COrder> COrdersView::GetOrder(uint256 const & orderTx) const
{
    return ReadBy<ByCreationTx, COrder>(orderTx);
}

void COrdersView::ForEachOrder(std::function<bool(uint256 const & orderTx, COrder const & order)> callback, uint256 const & start) const
{
    ForEach<ByCreationTx, uint256, COrder>([&callback] (uint256 const & orderTx, COrder const & orderImpl) {
        return callback(orderTx, orderImpl);
    }, start);
}

void COrdersView::ForEachExpiredOrder(std::function<bool (const uint256 &)> callback, uint32_t expiryHeight) const
{
    ForEach<ByExpiryHeight, ExpiredKey, char>([&] (ExpiredKey key, char) {
        if (key.height <= expiryHeight)  // 'equal' cause called for current blockheight, AFTER applying block txs (orders' job complete)
            return callback(key.orderTx);
        else
            return false;
    }, ExpiredKey{0, uint256()}); // from the very beginning!
}

Res COrdersView::DelOrder(uint256 const & orderTx)
{
    auto const order = GetOrder(orderTx);
    if (order) {
        EraseBy<ByCreationTx>(orderTx);
        if (order->timeInForce) {
            EraseBy<ByExpiryHeight>(ExpiredKey{(uint32_t)(order->creationHeight + order->timeInForce), orderTx});
        }
    }
    return Res::Ok();
}

Res COrdersView::SetOrder(uint256 const & orderTx, COrder const & order)
{
    WriteBy<ByCreationTx>(orderTx, order);
    if (order.timeInForce) {
        WriteBy<ByExpiryHeight>(ExpiredKey{(uint32_t)(order.creationHeight + order.timeInForce), orderTx}, '\0');
    }
    return Res::Ok();
}


