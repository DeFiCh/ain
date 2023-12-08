use serde::{Deserialize, Serialize};
#[derive(Debug, Default, Serialize, Deserialize)]
pub struct ScriptAggregation {
    pub id: String,
    pub hid: String,
    pub block: ScriptAggregationBlock,
    pub script: ScriptAggregationScript,
    pub statistic: ScriptAggregationStatistic,
    pub amount: ScriptAggregationAmount,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct ScriptAggregationBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct ScriptAggregationScript {
    pub r#type: String,
    pub hex: String,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct ScriptAggregationStatistic {
    pub tx_count: i32,
    pub tx_in_count: i32,
    pub tx_out_count: i32,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct ScriptAggregationAmount {
    pub tx_in: String,
    pub tx_out: String,
    pub unspent: String,
}
