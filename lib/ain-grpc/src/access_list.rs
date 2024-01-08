use ethereum::AccessList;
use ethereum_types::U256;

#[derive(Clone, Debug, Default, Eq, PartialEq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
#[serde(rename_all = "camelCase")]
pub struct AccessListResult {
    access_list: AccessList,
    gas_used: U256,
}

impl From<(AccessList, u64)> for AccessListResult {
    fn from(value: (AccessList, u64)) -> Self {
        Self {
            access_list: value.0,
            gas_used: value.1.into(),
        }
    }
}
