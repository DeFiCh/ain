use ain_db::{Column, ColumnName, TypedColumn};

#[derive(Debug)]
pub struct OraclePriceActive;

impl ColumnName for OraclePriceActive {
    const NAME: &'static str = "oracle_price_active";
}

impl Column for OraclePriceActive {
    type Index = String;
}

impl TypedColumn for OraclePriceActive {
    type Type = String;
}
