use crate::block::BlockNumber;
use crate::transaction_log::LogResult;
use primitive_types::{H160, H256};
use serde::{Serialize, Serializer};

#[derive(Clone, Debug, Default, Eq, PartialEq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
#[serde(rename_all = "camelCase")]
pub struct NewFilterRequest {
    pub address: Option<Vec<H160>>,
    pub from_block: Option<BlockNumber>,
    pub to_block: Option<BlockNumber>,
    pub topics: Option<Vec<H256>>,
}

#[derive(Debug, Deserialize, Clone, PartialEq)]
pub enum GetFilterChangesResult {
    Logs(Vec<LogResult>),
    NewBlock(Vec<H256>),
    NewPendingTransactions(Vec<H256>),
}

impl Serialize for GetFilterChangesResult {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match *self {
            GetFilterChangesResult::Logs(ref logs) => logs.serialize(serializer),
            GetFilterChangesResult::NewBlock(ref blocks) => blocks.serialize(serializer),
            GetFilterChangesResult::NewPendingTransactions(ref txs) => txs.serialize(serializer),
        }
    }
}
