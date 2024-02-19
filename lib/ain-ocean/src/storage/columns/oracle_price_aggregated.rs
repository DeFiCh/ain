use ain_db::{Column, ColumnName, TypedColumn};

use crate::model;

#[derive(Debug)]
pub struct OraclePriceAggregated;

impl ColumnName for OraclePriceAggregated {
    const NAME: &'static str = "oracle_price_aggregated";
}

impl Column for OraclePriceAggregated {
    type Index = model::OraclePriceAggregatedId;
}

impl TypedColumn for OraclePriceAggregated {
    type Type = model::OraclePriceAggregated;
}

pub struct OraclePriceAggregatedKey;

impl ColumnName for OraclePriceAggregatedKey {
    const NAME: &'static str = "oracle_price_aggregated_key";
}

impl Column for OraclePriceAggregatedKey {
    type Index = model::OraclePriceAggregatedKey;
}

impl TypedColumn for OraclePriceAggregatedKey {
    type Type = model::OraclePriceAggregatedId;
}
