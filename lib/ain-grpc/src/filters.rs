use crate::block::BlockNumber;
use primitive_types::{H160, H256};

#[derive(Clone, Debug, Default, Eq, PartialEq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
#[serde(rename_all = "camelCase")]
pub struct NewFilterRequest {
    pub address: Option<Vec<H160>>,
    pub from_block: Option<BlockNumber>,
    pub to_block: Option<BlockNumber>,
    pub topics: Option<Vec<H256>>,
}
