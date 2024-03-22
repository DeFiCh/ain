use ain_db::{Column, ColumnName, TypedColumn};

#[derive(Debug)]
pub struct OracleTokenCurrency;

impl ColumnName for OracleTokenCurrency {
    const NAME: &'static str = "oracle_token_currency";
}

impl Column for OracleTokenCurrency {
    type Index = String;
}

impl TypedColumn for OracleTokenCurrency {
    type Type = String;
}
