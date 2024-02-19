use ain_db::{Column, ColumnName, TypedColumn};

use crate::model::{OracleId, OracleKey, OraclePriceFeed as OraclePriceFeedMapper};
#[derive(Debug)]
pub struct OraclePriceFeed;

impl ColumnName for OraclePriceFeed {
    const NAME: &'static str = "oracle_price_feed";
}

impl Column for OraclePriceFeed {
    type Index = OracleId;
}

impl TypedColumn for OraclePriceFeed {
    type Type = OraclePriceFeedMapper;
}

pub struct OraclePriceFeedKey;

impl ColumnName for OraclePriceFeedKey {
    const NAME: &'static str = "oracle_price_feed_key";
}

impl Column for OraclePriceFeedKey {
    type Index = OracleKey;
}

impl TypedColumn for OraclePriceFeedKey {
    type Type = OracleId;
}
