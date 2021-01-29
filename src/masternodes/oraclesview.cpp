#include "masternodes/oraclesview.h"

const unsigned char COraclesView::ByName::prefix = 'O'; // the big O for Oracles

COraclesView::COraclesView(std::shared_ptr<CPriceFeedValidator> priceFeedValidator) :
        _validator(std::move(priceFeedValidator)) {
}

bool COraclesView::SetPriceFeedValue(const std::string &feedName, uint64_t timestamp, double rawPrice) {
    return WriteBy<ByName>(feedName, std::make_pair(timestamp, rawPrice));
}

ResVal<CPriceFeed> COraclesView::GetPriceFeedValue(const std::string &feedName) {
    CPriceFeed value;
    if (!ReadBy<ByName>(feedName, value)) {
        return Res::Err("failed to get price feed value %s", feedName);
    }

    return ResVal<CPriceFeed>(value, Res::Ok());
}

bool COraclesView::ExistPriceFeed(const std::string &feedName) const {
    return _validator->IsValidPriceFeedName(feedName);
}
