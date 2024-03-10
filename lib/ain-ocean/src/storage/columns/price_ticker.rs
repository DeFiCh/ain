use ain_db::{Column, ColumnName, TypedColumn};

#[derive(Debug)]
pub struct PriceTicker;

impl ColumnName for PriceTicker {
    const NAME: &'static str = "price_tiker";
}

impl Column for PriceTicker {
    type Index = String;
}

impl TypedColumn for PriceTicker {
    type Type = String;
}
