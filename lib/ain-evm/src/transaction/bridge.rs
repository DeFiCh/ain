use primitive_types::{H160, U256};

#[derive(Debug, Clone)]
pub enum BridgeTxType {
    EvmIn,
    EvmOut,
}

#[derive(Debug, Clone)]
pub struct BridgeTx {
    pub r#type: BridgeTxType,
    pub address: H160,
    pub amount: U256,
}
