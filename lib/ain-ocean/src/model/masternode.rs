use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
pub struct Masternode {
    pub id: String,
    pub sort: Option<String>,
    pub owner_address: String,
    pub operator_address: String,
    pub creation_height: u32,
    pub resign_height: i32,
    pub resign_tx: Option<String>,
    pub minted_blocks: i32,
    pub timelock: u16,
    pub collateral: String,
    pub block: MasternodeBlock,
    pub history: Option<Vec<HistoryItem>>,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct MasternodeBlock {
    pub hash: String,
    pub height: u32,
    pub time: u64,
    pub median_time: u64,
}

#[derive(Serialize, Deserialize, Debug, Default)]
enum MasternodeState {
    PreEnabled,
    Enabled,
    PreResigned,
    Resigned,
    PreBanned,
    Banned,
    #[default]
    Unknown,
}

impl std::fmt::Display for MasternodeState {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
       match self {
        MasternodeState::PreEnabled => write!(f, "PRE_ENABLED"),
        MasternodeState::Enabled => write!(f, "ENABLED"),
        MasternodeState::PreResigned => write!(f, "PRE_RESIGNED"),
        MasternodeState::Resigned => write!(f, "RESIGNED"),
        MasternodeState::PreBanned => write!(f, "PRE_BANNED"),
        MasternodeState::Banned => write!(f, "BANNED"),
        MasternodeState::Unknown => write!(f, "UNKNOWN"),
       } 
    }
}

#[derive(Serialize, Deserialize, Debug, Default)]
pub struct MasternodeOwner {
    pub address: String,
}

#[derive(Serialize, Deserialize, Debug, Default)]
pub struct MasternodeOperator {
    pub address: String,
}

#[derive(Serialize, Deserialize, Debug, Default)]
pub struct MasternodeCreation {
    pub height: i32,
}

#[derive(Serialize, Deserialize, Debug, Default)]
pub struct MasternodeResign {
    pub tx: String,
    pub height: i32,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct MasternodeData {
    pub id: String,
    pub sort: String,
    pub state: MasternodeState,
    pub minted_blocks: i32,
    pub owner: MasternodeOwner,
    pub operator: MasternodeOperator,
    pub creation: MasternodeCreation,
    pub resign: Option<MasternodeCreationResign>,
    pub timelock: i32,
}

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct HistoryItem {
    pub txid: String,
    pub owner_address: String,
    pub operator_address: String,
}
