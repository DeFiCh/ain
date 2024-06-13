use serde::{Deserialize, Serialize};

use super::{oracle_price_aggregated::OraclePriceAggregated, OraclePriceAggregatedApi};

pub type PriceTickerId = (String, String); //token-currency
pub type PriceTickerKey = (i32, u32, String, String); // total-height-token-currency

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PriceTicker {
    pub id: PriceTickerId, //token-currency
    pub sort: String,      //count-height-token-currency
    pub price: OraclePriceAggregated,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PriceTickerApi {
    pub id: String,   //token-currency
    pub sort: String, //count-height-token-currency
    pub price: OraclePriceAggregatedApi,
}
