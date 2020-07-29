// Copyright (c) 2019 DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_ORDERS_MATCHING_H
#define DEFI_MASTERNODES_ORDERS_MATCHING_H

#include <boost/optional.hpp>
#include <amount.h>
#include <masternodes/balances.h>
#include <masternodes/order.h>
#include <masternodes/res.h>

struct OrdersMatching {
    struct OrderDiff {
        CTokenAmount give; // doesn't include premium payment
        CTokenAmount take;
        CTokenAmount premiumGive;
    };

    static ResVal<OrdersMatching> Calculate(COrder const & aliceOrder, COrder const & carolOrder);

    static boost::optional<COrder> ApplyOrderDiff(COrder const & order, OrdersMatching::OrderDiff diff) {
        if (order.take.nValue == diff.take.nValue)
            return {}; // delete order
        auto newOrder = order;
        newOrder.take.nValue -= diff.take.nValue;
        newOrder.give.nValue -= diff.give.nValue;
        newOrder.premium.nValue -= diff.premiumGive.nValue;
        return newOrder;
    }

    OrderDiff alice;
    OrderDiff carol;

    CBalances matcherTake; // all matcher income, including premiums
};

#endif //DEFI_ORDERS_MATCHING_H
