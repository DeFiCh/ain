use serde::{Deserialize, Serialize};

use super::oracle_price_aggregated::OraclePriceAggregated;

pub type PriceTickerId = (String, String); //token-currency
#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PriceTicker {
    pub id: PriceTickerId, //token-currency
    pub sort: String,      //count-height-token-currency
    pub price: OraclePriceAggregated,
}
