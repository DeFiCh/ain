use ain_evm::filters::FilterResults;
use ethereum_types::{H160, H256};
use serde::{Serialize, Serializer};
use serde_with::{serde_as, OneOrMany};

use crate::{
    block::BlockNumber,
    logs::{LogRequestTopics, LogResult},
};

#[serde_as]
#[derive(Clone, Debug, Default, Eq, PartialEq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
#[serde(rename_all = "camelCase")]
pub struct NewFilterRequest {
    #[serde_as(as = "Option<OneOrMany<_>>")]
    pub address: Option<Vec<H160>>,
    pub from_block: Option<BlockNumber>,
    pub to_block: Option<BlockNumber>,
    pub topics: Option<LogRequestTopics>,
}

#[derive(Debug, Deserialize, Clone, PartialEq)]
pub enum GetFilterChangesResult {
    Logs(Vec<LogResult>),
    NewBlock(Vec<H256>),
    NewPendingTransactions(Vec<H256>),
}

impl From<FilterResults> for GetFilterChangesResult {
    fn from(result: FilterResults) -> Self {
        match result {
            FilterResults::Logs(f) => {
                GetFilterChangesResult::Logs(f.into_iter().map(|log| log.into()).collect())
            }
            FilterResults::Blocks(f) => GetFilterChangesResult::NewBlock(f),
            FilterResults::Transactions(f) => GetFilterChangesResult::NewPendingTransactions(f),
        }
    }
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
