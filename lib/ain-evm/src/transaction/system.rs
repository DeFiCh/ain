#[derive(Debug, Clone)]
pub struct DeployContractData {
    pub name: String,
    pub symbol: String,
}

#[derive(Debug, Clone)]
pub enum SystemTx {
    DeployContract(DeployContractData),
}
