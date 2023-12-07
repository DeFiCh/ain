#[derive(Debug, Default)]
pub struct Oracle {
    pub id: String,
    pub owner_address: String,
    pub weightage: i32,
    pub price_feeds: Vec<PriceFeedsItem>,
    pub block: OracleBlock,
}

#[derive(Debug, Default)]
pub struct PriceFeedsItem {
    pub token: String,
    pub currency: String,
}

#[derive(Debug, Default)]
pub struct OracleBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}
