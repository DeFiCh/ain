use serde::{Deserialize, Serialize};

use super::BlockContext;
pub type OraclePriceAggregatedIntervalId = (String, String, OracleIntervalSeconds, u32); //token-currency-interval-height
pub type OraclePriceAggregatedIntervalKey = (String, String, OracleIntervalSeconds); //token-currency-interval

pub const FIFTEEN_MINUTES: isize = 15 * 60;
pub const ONE_HOUR: isize = 60 * 60;
pub const ONE_DAY: isize = 24 * 60 * 60;

#[derive(Serialize, Deserialize, Debug, Clone)]
pub enum OracleIntervalSeconds {
    FifteenMinutes = FIFTEEN_MINUTES,
    OneHour = ONE_HOUR,
    OneDay = ONE_DAY,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedInterval {
    pub id: OraclePriceAggregatedIntervalId,
    pub key: OraclePriceAggregatedIntervalKey,
    pub sort: String,
    pub token: String,
    pub currency: String,
    pub aggregated: OraclePriceAggregatedIntervalAggregated,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedIntervalAggregated {
    pub amount: String,
    pub weightage: i32,
    pub count: i32,
    pub oracles: OraclePriceAggregatedIntervalAggregatedOracles,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedIntervalAggregatedOracles {
    pub active: i32,
    pub total: i32,
}
