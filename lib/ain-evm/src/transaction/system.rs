use primitive_types::H160;

#[derive(Debug, Clone)]
pub struct DeployContractData {
    pub name: String,
    pub symbol: String,
    pub address: H160,
}

#[derive(Debug, Clone)]
pub enum SystemTx {
    DeployContract(DeployContractData),
}
