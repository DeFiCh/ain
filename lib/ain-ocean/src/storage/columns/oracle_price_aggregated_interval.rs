use ain_db::{Column, ColumnName, TypedColumn};

use crate::model;

#[derive(Debug)]
pub struct OraclePriceAggregatedInterval;

impl ColumnName for OraclePriceAggregatedInterval {
    const NAME: &'static str = "oracle_price_aggregated_interval";
}

impl Column for OraclePriceAggregatedInterval {
    type Index = model::OraclePriceAggregatedIntervalId;
}

impl TypedColumn for OraclePriceAggregatedInterval {
    type Type = model::OraclePriceAggregatedInterval;
}

#[derive(Debug)]
pub struct OraclePriceAggregatedIntervalKey;

impl ColumnName for OraclePriceAggregatedIntervalKey {
    const NAME: &'static str = "oracle_price_aggregated_interval_key";
}

impl Column for OraclePriceAggregatedIntervalKey {
    type Index = model::OraclePriceAggregatedIntervalKey;
}

impl TypedColumn for OraclePriceAggregatedIntervalKey {
    type Type = model::OraclePriceAggregatedIntervalId;
}
