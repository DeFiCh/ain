use bitcoin::{hashes::Hash, Txid};

use crate::{model::TxResult, repository::RepositoryOps, Result, SERVICES};

pub fn index(tx_type: u8, tx_hash: [u8; 32], result_ptr: usize) -> Result<()> {
    let txid = Txid::from_byte_array(tx_hash);
    let result = TxResult::from((tx_type, result_ptr));
    println!("txid : {:?}", txid);
    println!("result : {:?}", result);
    SERVICES.result.put(&txid, &result)
}

pub fn invalidate(txid: &Txid) -> Result<()> {
    SERVICES.result.delete(txid)
}
