use std::sync::Arc;

use bitcoin::{hashes::Hash, Txid};

use crate::{model::TxResult, repository::RepositoryOps, Result, Services};

pub fn index(
    services: Arc<Services>,
    tx_type: u8,
    tx_hash: [u8; 32],
    result_ptr: usize,
) -> Result<()> {
    let txid = Txid::from_byte_array(tx_hash);
    let result = TxResult::from((tx_type, result_ptr));
    services.result.put(&txid, &result)
}

pub fn invalidate(services: Arc<Services>, txid: &Txid) -> Result<()> {
    services.result.delete(txid)
}
