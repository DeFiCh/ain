use primitive_types::{H160, U256};

#[derive(Debug, Clone)]
pub struct DeployContractData {
    pub name: String,
    pub symbol: String,
    pub address: H160,
}

#[derive(Debug, Clone)]
pub struct DST20InData {
    pub to: H160,
    pub contract: H160,
    pub amount: U256,
}

#[derive(Debug, Clone)]
pub enum SystemTx {
    DeployContract(DeployContractData),
    DST20In(DST20InData),
}
