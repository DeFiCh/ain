use ain_db::{Column, ColumnName, TypedColumn};

#[derive(Debug)]
pub struct OraclePriceAggregated;

impl ColumnName for OraclePriceAggregated {
    const NAME: &'static str = "oracle_price_aggregated";
}

impl Column for OraclePriceAggregated {
    type Index = String;
}

impl TypedColumn for OraclePriceAggregated {
    type Type = String;
}
