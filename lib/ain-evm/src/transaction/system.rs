use primitive_types::{H160, U256};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DeployContractData {
    pub name: String,
    pub symbol: String,
    pub address: H160,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DST20Data {
    pub to: H160,
    pub contract: H160,
    pub amount: U256,
    pub out: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BalanceUpdate {
    pub address: H160,
    pub amount: U256,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SystemTx {
    DeployContract(DeployContractData),
    DST20Bridge(DST20Data),
    EvmIn(BalanceUpdate),
    EvmOut(BalanceUpdate),
}

impl SystemTx {
    pub fn sender(&self) -> Option<H160> {
        match self {
            SystemTx::EvmIn(tx) => Some(tx.address),
            SystemTx::EvmOut(tx) => Some(tx.address),
            _ => None,
        }
    }
}
