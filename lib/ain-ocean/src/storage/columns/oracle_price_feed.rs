use ain_db::{Column, ColumnName, TypedColumn};

use crate::model::{
    OraclePriceFeed as OraclePriceFeedMapper, OraclePriceFeedId, OraclePriceFeedkey,
};
#[derive(Debug)]
pub struct OraclePriceFeed;

impl ColumnName for OraclePriceFeed {
    const NAME: &'static str = "oracle_price_feed";
}

impl Column for OraclePriceFeed {
    type Index = OraclePriceFeedId;
}

impl TypedColumn for OraclePriceFeed {
    type Type = OraclePriceFeedMapper;
}

pub struct OraclePriceFeedKey;

impl ColumnName for OraclePriceFeedKey {
    const NAME: &'static str = "oracle_price_feed_key";
}

impl Column for OraclePriceFeedKey {
    type Index = OraclePriceFeedkey;
}

impl TypedColumn for OraclePriceFeedKey {
    type Type = OraclePriceFeedId;
}
