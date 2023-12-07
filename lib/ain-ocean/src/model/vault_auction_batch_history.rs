#[derive(Debug, Default)]
pub struct VaultAuctionBatchHistory {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub vault_id: String,
    pub index: i32,
    pub from: String,
    pub amount: String,
    pub token_id: i32,
    pub block: VaultAuctionBatchHistoryBlock,
}

#[derive(Debug, Default)]
pub struct VaultAuctionBatchHistoryBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}
