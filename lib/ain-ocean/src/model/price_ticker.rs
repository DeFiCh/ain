use serde::{Deserialize, Serialize};

use super::oracle_price_aggregated::OraclePriceAggregated;

#[derive(Debug, Default)]
pub struct PriceTicker {
    pub id: String,
    pub sort: String,
    pub price: OraclePriceAggregated,
}
