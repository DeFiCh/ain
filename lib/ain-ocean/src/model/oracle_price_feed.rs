#[derive(Debug, Default)]
pub struct OraclePriceFeed {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub token: String,
    pub currency: String,
    pub oracle_id: String,
    pub txid: String,
    pub time: i32,
    pub amount: String,
    pub block: OraclePriceFeedBlock,
}

#[derive(Debug, Default)]
pub struct OraclePriceFeedBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}
