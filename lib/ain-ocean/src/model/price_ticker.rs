use ain_dftx::{Currency, Token};
use serde::{Deserialize, Serialize};

use super::oracle_price_aggregated::OraclePriceAggregated;

pub type PriceTickerId = ([u8; 4], [u8; 4], Token, Currency); // total-height-token-currency

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PriceTicker {
    pub price: OraclePriceAggregated,
}
