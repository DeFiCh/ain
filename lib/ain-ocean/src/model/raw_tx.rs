use rust_decimal::Decimal;
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct RawTx {
    pub hex: String,
    pub max_fee_rate: Option<Decimal>,
}
