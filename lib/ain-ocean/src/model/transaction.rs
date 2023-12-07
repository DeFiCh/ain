#[derive(Debug, Default)]
pub struct Transaction {
    pub id: String,
    pub order: i32,
    pub block: TransactionBlock,
    pub txid: String,
    pub hash: String,
    pub version: i32,
    pub size: i32,
    pub v_size: i32,
    pub weight: i32,
    pub total_vout_value: String,
    pub lock_time: i32,
    pub vin_count: i32,
    pub vout_count: i32,
}

#[derive(Debug, Default)]
pub struct TransactionBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}
