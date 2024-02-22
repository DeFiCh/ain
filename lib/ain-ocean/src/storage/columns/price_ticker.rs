use ain_db::{Column, ColumnName, TypedColumn};

use crate::model;

#[derive(Debug)]
pub struct PriceTicker;

impl ColumnName for PriceTicker {
    const NAME: &'static str = "price_ticker";
}

impl Column for PriceTicker {
    type Index = model::PriceTickerId;
}

impl TypedColumn for PriceTicker {
    type Type = model::PriceTicker;
}
