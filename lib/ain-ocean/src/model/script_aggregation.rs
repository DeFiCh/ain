use serde::{Deserialize, Serialize};

use super::BlockContext;

pub type ScriptAggregationId = (String, u32); // (hid, block.height)

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct ScriptAggregation {
    pub id: ScriptAggregationId,
    pub hid: String,
    pub block: BlockContext,
    pub script: ScriptAggregationScript,
    pub statistic: ScriptAggregationStatistic,
    pub amount: ScriptAggregationAmount,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct ScriptAggregationScript {
    pub r#type: String,
    pub hex: Vec<u8>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct ScriptAggregationStatistic {
    pub tx_count: i32,
    pub tx_in_count: i32,
    pub tx_out_count: i32,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct ScriptAggregationAmount {
    pub tx_in: String,
    pub tx_out: String,
    pub unspent: String,
}
