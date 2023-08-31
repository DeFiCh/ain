use primitive_types::{H160, U256};

use super::SignedTx;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DeployContractData {
    pub name: String,
    pub symbol: String,
    pub address: H160,
    pub token_id: u64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DST20Data {
    pub to: H160,
    pub contract: H160,
    pub amount: U256,
    pub out: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SystemTx {
    DeployContract(DeployContractData),
    DST20Bridge(DST20Data),
    EvmIn(Box<SignedTx>),
    EvmOut(Box<SignedTx>),
}

impl SystemTx {
    pub fn sender(&self) -> Option<H160> {
        match self {
            SystemTx::EvmIn(tx) | SystemTx::EvmOut(tx) => Some(tx.sender),
            _ => None,
        }
    }
}
