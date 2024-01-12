use dftx_rs::custom_tx::CustomTxType;
use serde::{Deserialize, Serialize};

#[repr(C)]
#[derive(Debug, Serialize, Deserialize, Clone, Copy)]
pub struct PoolSwapResult {
    pub to_amount: i64,
    pub pool_id: u32,
}

#[derive(Serialize, Deserialize, Debug)]
pub enum TxResult {
    PoolSwap(PoolSwapResult),
    None,
}

impl From<(u8, usize)> for TxResult {
    fn from((tx_type, result_ptr): (u8, usize)) -> Self {
        let dftx = CustomTxType::from(tx_type);

        match dftx {
            CustomTxType::PoolSwap | CustomTxType::PoolSwapV2 => {
                TxResult::PoolSwap(unsafe { *(result_ptr as *const PoolSwapResult) })
            }
            _ => TxResult::None,
        }
    }
}
