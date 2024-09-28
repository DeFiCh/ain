use std::collections::HashMap;

use rust_decimal::Decimal;
use serde::{Deserialize, Serialize};

use super::BlockContext;

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct MasternodeStats {
    pub block: BlockContext,
    pub stats: MasternodeStatsData,
}

#[derive(Serialize, Deserialize, Debug, Clone, Default)]
#[serde(rename_all = "camelCase")]
pub struct TimelockStats {
    #[serde(with = "rust_decimal::serde::str")]
    pub tvl: Decimal,
    pub count: u32,
}

#[derive(Serialize, Deserialize, Debug, Clone, Default)]
#[serde(rename_all = "camelCase")]
pub struct MasternodeStatsData {
    pub count: u32,
    #[serde(with = "rust_decimal::serde::str")]
    pub tvl: Decimal,
    pub locked: HashMap<u16, TimelockStats>,
}
