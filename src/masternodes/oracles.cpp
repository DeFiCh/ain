#include "masternodes/oracles.h"


const unsigned char COracleView::ByName::prefix = 'O'; // the big O for Oracles

COracleView::COracleView(std::shared_ptr<CPriceFeedNameValidator> priceFeedValidator) :
        _validator(std::move(priceFeedValidator)) {
}

bool COracleView::SetPriceFeedValue(const std::string &feedName, CTimeStamp timestamp, double rawPrice) {
    return WriteBy<ByName>(feedName, std::make_pair(timestamp, rawPrice));
}

ResVal<CPriceFeed> COracleView::GetPriceFeedValue(const std::string &feedName) {
    CPriceFeed value;
    if (!ReadBy<ByName>(feedName, value)) {
        return Res::Err("failed to get price feed value %s", feedName);
    }

    return ResVal<CPriceFeed>(value, Res::Ok());
}

bool COracleView::ExistPriceFeed(const std::string &feedName) const {
    return _validator->IsValidPriceFeedName(feedName);
}
