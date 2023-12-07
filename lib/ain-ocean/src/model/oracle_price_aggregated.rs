#[derive(Debug, Default)]
pub struct OraclePriceAggregated {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub token: String,
    pub currency: String,
    pub aggregated: OraclePriceAggregatedAggregated,
    pub block: OraclePriceAggregatedBlock,
}

#[derive(Debug, Default)]
pub struct OraclePriceAggregatedAggregated {
    pub amount: String,
    pub weightage: i32,
    pub oracles: OraclePriceAggregatedAggregatedOracles,
}

#[derive(Debug, Default)]
pub struct OraclePriceAggregatedBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}

#[derive(Debug, Default)]
pub struct OraclePriceAggregatedAggregatedOracles {
    pub active: i32,
    pub total: i32,
}
