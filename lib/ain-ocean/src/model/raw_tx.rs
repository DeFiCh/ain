use rust_decimal::Decimal;
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default, Clone)]
#[serde(rename_all = "camelCase")]
pub struct RawTxDto {
    pub hex: String,
    pub max_fee_rate: Option<Decimal>,
}
