use serde::{Deserialize, Serialize};

use super::oracle_price_aggregated::OraclePriceAggregated;

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct PriceTicker {
    pub id: String,
    pub sort: String,
    pub price: OraclePriceAggregated,
}
