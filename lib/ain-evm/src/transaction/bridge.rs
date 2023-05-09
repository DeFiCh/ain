use primitive_types::{H160, U256};

#[derive(Debug)]
pub struct BalanceUpdate {
    pub address: H160,
    pub amount: U256,
}

#[derive(Debug)]
pub enum BridgeTx {
    EvmIn(BalanceUpdate),
    EvmOut(BalanceUpdate),
}
