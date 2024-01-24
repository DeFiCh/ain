use ain_db::{Column, ColumnName, TypedColumn};

use crate::model::OraclePriceFeed as OraclePriceFeedMapper;
#[derive(Debug)]
pub struct OraclePriceFeed;

impl ColumnName for OraclePriceFeed {
    const NAME: &'static str = "oracle_price_feed";
}

impl Column for OraclePriceFeed {
    type Index = String;
}

impl TypedColumn for OraclePriceFeed {
    type Type = OraclePriceFeedMapper;
}
