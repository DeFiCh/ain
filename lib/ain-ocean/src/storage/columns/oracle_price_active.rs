use ain_db::{Column, ColumnName, TypedColumn};

use crate::model;

#[derive(Debug)]
pub struct OraclePriceActive;

impl ColumnName for OraclePriceActive {
    const NAME: &'static str = "oracle_price_active";
}

impl Column for OraclePriceActive {
    type Index = model::OraclePriceActiveId;
}

impl TypedColumn for OraclePriceActive {
    type Type = model::OraclePriceActive;
}

#[derive(Debug)]
pub struct OraclePriceActiveKey;

impl ColumnName for OraclePriceActiveKey {
    const NAME: &'static str = "oracle_price_active_key";
}

impl Column for OraclePriceActiveKey {
    type Index = model::OraclePriceActiveKey;
}

impl TypedColumn for OraclePriceActiveKey {
    type Type = model::OraclePriceActiveId;
}

