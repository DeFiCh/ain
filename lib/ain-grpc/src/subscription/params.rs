use ethereum_types::{H160, H256};
use serde::Deserialize;
use serde_with::{serde_as, OneOrMany};

/// Subscription kind.
#[derive(Clone, Debug, Eq, PartialEq, Hash, Deserialize)]
#[serde(deny_unknown_fields)]
#[serde(rename_all = "camelCase")]
pub enum Subscription {
    /// New block headers subscription.
    NewHeads,
    /// Logs subscription.
    Logs,
    /// New Pending Transactions subscription.
    NewPendingTransactions,
    /// Node syncing status subscription.
    Syncing,
}

#[serde_as]
#[derive(Clone, Debug, Eq, PartialEq, Hash, Deserialize)]
#[serde(deny_unknown_fields)]
#[serde(rename_all = "camelCase")]
pub struct SubscriptionParams {
    #[serde_as(as = "Option<OneOrMany<_>>")]
    pub address: Option<Vec<H160>>,
    pub topics: Option<SubscriptionParamsTopics>,
}

#[derive(Clone, Debug, Eq, PartialEq, Hash, Deserialize)]
#[serde(untagged)]
pub enum SubscriptionParamsTopics {
    VecOfHashes(Vec<Option<H256>>),
    VecOfHashVecs(Vec<Vec<Option<H256>>>),
}
