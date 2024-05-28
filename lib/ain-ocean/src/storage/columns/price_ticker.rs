use ain_db::{Column, ColumnName, DBError, TypedColumn};

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

#[derive(Debug)]
pub struct PriceTickerKey;

impl ColumnName for PriceTickerKey {
    const NAME: &'static str = "price_ticker_key";
}

impl Column for PriceTickerKey {
    type Index = model::PriceTickerKey;

    fn key(index: &Self::Index) -> Result<Vec<u8>, DBError> {
        let (total, height, token, currency) = index;
        let mut vec = Vec::new();
        vec.extend_from_slice(&total.to_be_bytes());
        vec.extend_from_slice(&height.to_be_bytes());
        vec.extend_from_slice(token.as_bytes());
        vec.extend_from_slice(currency.as_bytes());
        Ok(vec)
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        let total = i32::from_be_bytes(raw_key[0..4].try_into().unwrap());
        let height = u32::from_be_bytes(raw_key[4..8].try_into().unwrap());
        let currency_n = raw_key.len() - 3; // 3 letters of currency code
        let token = std::str::from_utf8(&raw_key[8..currency_n]).map_err(|_| DBError::ParseKey)?.to_string();
        let currency = std::str::from_utf8(&raw_key[currency_n..]).map_err(|_| DBError::ParseKey)?.to_string();
        Ok((total, height, token, currency))
    }
}

impl TypedColumn for PriceTickerKey {
    type Type = model::PriceTickerId;
}

