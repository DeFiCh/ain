use ain_db::{Column, ColumnName, TypedColumn};

use crate::{api::prices::PriceKey, model};

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

#[derive(Debug)]
pub struct PriceTickerKey;

impl ColumnName for PriceTickerKey {
    const NAME: &'static str = "price_ticker_key";
}

impl Column for PriceTickerKey {
    type Index = PriceKey;
}

impl TypedColumn for PriceTickerKey {
    type Type = model::PriceTickerId;
}
