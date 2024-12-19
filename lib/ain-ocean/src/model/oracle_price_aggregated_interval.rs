use ain_dftx::{Currency, Token};
use rust_decimal::Decimal;
use serde::{Deserialize, Serialize};

use super::BlockContext;
pub type OraclePriceAggregatedIntervalId = (Token, Currency, String, [u8; 4]); //token-currency-interval-height

#[derive(Debug, Clone, PartialEq)]
pub enum OracleIntervalSeconds {
    FifteenMinutes = 900,
    OneHour = 3600,
    OneDay = 86400,
}

impl Serialize for OracleIntervalSeconds {
    fn serialize<S>(&self, serializer: S) -> std::result::Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        match self {
            Self::FifteenMinutes => serializer.serialize_str("900"),
            Self::OneHour => serializer.serialize_str("3600"),
            Self::OneDay => serializer.serialize_str("86400"),
        }
    }
}

impl<'a> Deserialize<'a> for OracleIntervalSeconds {
    fn deserialize<D>(deserializer: D) -> std::result::Result<Self, D::Error>
    where
        D: serde::Deserializer<'a>,
    {
        let s = String::deserialize(deserializer).unwrap();
        if s == *"900" {
            Ok(Self::FifteenMinutes)
        } else if s == *"3600" {
            Ok(Self::OneHour)
        } else {
            Ok(Self::OneDay)
        }
    }
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
    #[serde(with = "rust_decimal::serde::str")]
    pub amount: Decimal,
    #[serde(with = "rust_decimal::serde::str")]
    pub weightage: Decimal,
    pub count: i32,
    pub oracles: OraclePriceAggregatedIntervalAggregatedOracles,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct OraclePriceAggregatedIntervalAggregatedOracles {
    #[serde(with = "rust_decimal::serde::str")]
    pub active: Decimal,
    pub total: i32,
}
