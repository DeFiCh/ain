use primitive_types::{H160, U256};

#[derive(Debug, Clone)]
pub struct BalanceUpdate {
    pub address: H160,
    pub amount: U256,
}

#[derive(Debug, Clone)]
pub enum BridgeTx {
    EvmIn(BalanceUpdate),
    EvmOut(BalanceUpdate),
}

impl BridgeTx {
    pub fn sender(&self) -> H160 {
        match self {
            BridgeTx::EvmIn(tx) => tx.address,
            BridgeTx::EvmOut(tx) => tx.address,
        }
    }
}
