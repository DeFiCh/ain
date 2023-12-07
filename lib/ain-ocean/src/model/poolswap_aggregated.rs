#[derive(Debug, Default)]
pub struct PoolSwapAggregated {
    pub id: String,
    pub key: String,
    pub bucket: i32,
    pub aggregated: PoolSwapAggregatedAggregated,
    pub block: PoolSwapAggregatedBlock,
}

#[derive(Debug, Default)]
pub struct PoolSwapAggregatedAggregated {
    pub amounts: Vec<String>,
}

#[derive(Debug, Default)]
pub struct PoolSwapAggregatedBlock {
    pub median_time: i32,
}
