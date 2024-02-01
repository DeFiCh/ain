use ain_db::{Column, ColumnName, TypedColumn};

use crate::model;
#[derive(Debug)]
pub struct OracleTokenCurrency;

impl ColumnName for OracleTokenCurrency {
    const NAME: &'static str = "oracle_token_currency";
}

impl Column for OracleTokenCurrency {
    type Index = model::OracleTokenCurrencyId;
}

impl TypedColumn for OracleTokenCurrency {
    type Type = model::OracleTokenCurrency;
}

pub struct OracleTokenCurrencyKey;

impl ColumnName for OracleTokenCurrencyKey {
    const NAME: &'static str = "oracle_token_currency_key";
}

impl Column for OracleTokenCurrencyKey {
    type Index = model::OracleTokenCurrencyKey;
}

impl TypedColumn for OracleTokenCurrencyKey {
    type Type = model::OracleTokenCurrencyId;
}
