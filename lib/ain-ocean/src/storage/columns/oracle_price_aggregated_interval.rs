use ain_db::{Column, ColumnName, TypedColumn};

#[derive(Debug)]
pub struct OraclePriceAggregatedInterval;

impl ColumnName for OraclePriceAggregatedInterval {
    const NAME: &'static str = "oracle_price_aggregated_interval";
}

impl Column for OraclePriceAggregatedInterval {
    type Index = String;
}

impl TypedColumn for OraclePriceAggregatedInterval {
    type Type = String;
}
