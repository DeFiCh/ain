use ain_dftx::{Currency, Token};
use serde::{Deserialize, Serialize};

use super::BlockContext;
pub type OraclePriceAggregatedIntervalId = (Token, Currency, OracleIntervalSeconds, u32); //token-currency-interval-height
pub type OraclePriceAggregatedIntervalKey = (Token, Currency, OracleIntervalSeconds); //token-currency-interval

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
    pub aggregated: OraclePriceAggregatedIntervalAggregated,
    pub block: BlockContext,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedIntervalAggregated {
    pub amount: String,
    pub weightage: u32,
    pub count: i32,
    pub oracles: OraclePriceAggregatedIntervalAggregatedOracles,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedIntervalAggregatedOracles {
    pub active: i32,
    pub total: i32,
}
