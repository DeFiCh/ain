#include "masternodes/oracles.h"

const unsigned char COracleView::ByName::prefix = 'O'; // the big O for Oracles

//bool COracleView::SetPriceFeedValue(const std::string &feedName, CTimeStamp timestamp, double rawPrice) {
//    return WriteBy<ByName>(feedName, std::make_pair(timestamp, rawPrice));
//}

//ResVal<CPriceFeed> COracleView::GetTokenRawPrice(const std::string &feedName) {
//    CPriceFeed value;
//    if (!ReadBy<ByName>(feedName, value)) {
//        return Res::Err("failed to get price feed value %s", feedName);
//    }
//
//    return ResVal<CPriceFeed>(value, Res::Ok());
//}


Res COracleView::RegisterOracle(const COracle& oracle) {

}

Res COracleView::SetOracleData(COracleId oracleId, const CTokenPrices& tokenPrices) {

}

ResVal<CRawPrice> COracleView::GetTokenRawPrice(DCT_ID tokenId, COracleId oracleId) {

}

Res COracleView::SetTokenRawPrice(COracleId oracleId, DCT_ID tokenId, CRawPrice rawPrice) {

}
