use serde::{Serialize, Deserialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct PriceTicker {
    pub id: String,
    pub sort: String,
    pub price: OraclePriceAggregated,
}



