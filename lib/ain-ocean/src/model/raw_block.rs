use serde::{Serialize, Deserialize};

#[derive(Serialize, Deserialize, Debug, Default)]
#[serde(rename_all = "camelCase")]
pub struct RawBlock {
    pub id: String,
    pub json: String,
}



