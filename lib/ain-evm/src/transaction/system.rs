use crate::bytes::Bytes;
use primitive_types::{H160, H256};

#[derive(Debug, Clone)]
pub struct DeployContractData {
    pub bytecode: Bytes,
    pub storage: Vec<(H256, H256)>,
    pub address: H160,
}

#[derive(Debug, Clone)]
pub enum SystemTx {
    DeployContract(DeployContractData),
}
