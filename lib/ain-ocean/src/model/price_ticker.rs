use serde::{Deserialize, Serialize};

use super::oracle_price_aggregated::OraclePriceAggregated;

pub type PriceTickerId = (String, String); //token-currency
pub type PriceTickerKey = ([u8; 4], [u8; 4], String, String); // total-height-token-currency

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PriceTicker {
    pub price: OraclePriceAggregated,
}
