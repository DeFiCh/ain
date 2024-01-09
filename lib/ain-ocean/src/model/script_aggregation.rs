use serde::{Deserialize, Serialize};

use super::BlockContext;
#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptAggregation {
    pub id: String,
    pub hid: String,
    pub block: BlockContext,
    pub script: ScriptAggregationScript,
    pub statistic: ScriptAggregationStatistic,
    pub amount: ScriptAggregationAmount,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptAggregationScript {
    pub r#type: String,
    pub hex: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptAggregationStatistic {
    pub tx_count: i32,
    pub tx_in_count: i32,
    pub tx_out_count: i32,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ScriptAggregationAmount {
    pub tx_in: String,
    pub tx_out: String,
    pub unspent: String,
}
