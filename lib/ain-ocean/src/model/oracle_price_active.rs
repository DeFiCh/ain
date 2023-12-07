#[derive(Debug, Default)]
pub struct OraclePriceActive {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub active: OraclePriceActiveActive,
    pub next: OraclePriceActiveNext,
    pub is_live: bool,
    pub block: OraclePriceActiveBlock,
}

#[derive(Debug, Default)]
pub struct OraclePriceActiveActive {
    pub amount: String,
    pub weightage: i32,
    pub oracles: OraclePriceActiveActiveOracles,
}

#[derive(Debug, Default)]
pub struct OraclePriceActiveNext {
    pub amount: String,
    pub weightage: i32,
    pub oracles: OraclePriceActiveNextOracles,
}

#[derive(Debug, Default)]
pub struct OraclePriceActiveBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}

#[derive(Debug, Default)]
pub struct OraclePriceActiveActiveOracles {
    pub active: i32,
    pub total: i32,
}

#[derive(Debug, Default)]
pub struct OraclePriceActiveNextOracles {
    pub active: i32,
    pub total: i32,
}
