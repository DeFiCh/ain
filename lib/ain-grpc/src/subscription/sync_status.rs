use ethereum_types::U256;

#[derive(Clone, Debug, Serialize)]
#[serde(untagged)]
pub enum PubSubSyncStatus {
    Simple(bool),
    Detailed(SyncStatusMetadata),
}

impl PartialEq for PubSubSyncStatus {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (PubSubSyncStatus::Simple(s1), PubSubSyncStatus::Simple(s2)) => s1 == s2,
            (PubSubSyncStatus::Detailed(d1), PubSubSyncStatus::Detailed(d2)) => {
                d1.syncing == d2.syncing
            }
            _ => false,
        }
    }
}

impl Eq for PubSubSyncStatus {}

/// PubSbub sync status
#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SyncStatusMetadata {
    pub syncing: bool,
    pub starting_block: U256,
    pub current_block: U256,
    #[serde(default = "Default::default", skip_serializing_if = "Option::is_none")]
    pub highest_block: Option<U256>,
}
