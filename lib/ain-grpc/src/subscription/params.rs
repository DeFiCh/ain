use ethereum_types::{H160, H256};
use serde::{de::Error, Deserialize, Deserializer};
use serde_json::{from_value, Value};
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
pub struct LogsSubscriptionParams {
    #[serde_as(as = "Option<OneOrMany<_>>")]
    pub address: Option<Vec<H160>>,
    pub topics: Option<LogsSubscriptionParamsTopics>,
}

#[derive(Clone, Debug, Eq, PartialEq, Hash, Serialize, Deserialize)]
#[serde(untagged)]
pub enum LogsSubscriptionParamsTopics {
    VecOfHashes(Vec<Option<H256>>),
    VecOfHashVecs(Vec<Vec<Option<H256>>>),
}

/// Subscription kind.
#[derive(Clone, Debug, Eq, PartialEq, Hash, Default)]
pub enum SubscriptionParams {
    /// No parameters passed.
    #[default]
    None,
    // Log parameters.
    Logs(LogsSubscriptionParams),
}

impl<'a> Deserialize<'a> for SubscriptionParams {
    fn deserialize<D>(deserializer: D) -> Result<SubscriptionParams, D::Error>
    where
        D: Deserializer<'a>,
    {
        let v: Value = Deserialize::deserialize(deserializer)?;

        if v.is_null() {
            return Ok(SubscriptionParams::None);
        }

        from_value(v)
            .map(SubscriptionParams::Logs)
            .map_err(|e| Error::custom(format!("Invalid logs parameters: {}", e)))
    }
}
