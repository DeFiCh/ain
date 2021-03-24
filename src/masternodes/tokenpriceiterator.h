// Copyright (c) DeFi Blockchain Developers
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFI_MASTERNODES_TOKENPRICEITERATOR_H
#define DEFI_MASTERNODES_TOKENPRICEITERATOR_H

#include <functional>

#include <masternodes/oracles.h>

class CCustomCSView;

class TokenPriceIterator
{
public:
    TokenPriceIterator(
        std::reference_wrapper<const COracleView> view,
        int64_t lastBlockTime) : _view{view}, _lastBlockTime{lastBlockTime}
    {
    }

    // clang-format off
    /// visitor signature
    using Visitor = std::function<Res(
            const COracleId &   // oracle id
            , DCT_ID            // token id
            , CURRENCY_ID       // currency id
            , int64_t timeStamp // oracle timestamp
            , CAmount           // token raw price
            , uint8_t weightage // oracle weightage
            , OracleState       // oracle state: live or expired
            )>;
    // clang-format on

    /// @brief Iterate through all oracles and their data and visit each data item
    /// @param tokenId if initialized, only tokens of specified id will be visited
    /// @param currencyId if initialized, only specified currency will be considered
    Res ForEach(const Visitor& visitor,
        boost::optional<TokenCurrencyPair> filter = boost::none);

private:
    std::reference_wrapper<const COracleView> _view;
    int64_t _lastBlockTime;
};

#endif // DEFI_MASTERNODES_TOKENPRICEITERATOR_H
