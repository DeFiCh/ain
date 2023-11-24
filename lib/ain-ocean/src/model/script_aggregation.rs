use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct ScriptAggregation {
    pub id: String,
    pub hid: String,
    pub block: ScriptAggregationBlock,
    pub script: ScriptAggregationScript,
    pub statistic: ScriptAggregationStatistic,
    pub amount: ScriptAggregationAmount,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct ScriptAggregationBlock {
    pub hash: String,
    pub height: i32,
    pub time: i32,
    pub median_time: i32,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct ScriptAggregationScript {
    pub r#type: String,
    pub hex: String,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct ScriptAggregationStatistic {
    pub tx_count: i32,
    pub tx_in_count: i32,
    pub tx_out_count: i32,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct ScriptAggregationAmount {
    pub tx_in: String,
    pub tx_out: String,
    pub unspent: String,
}
