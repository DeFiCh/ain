use defichain_rpc::json::blockchain::{Block, Transaction};
use std::sync::Arc;

use crate::{Result, Services};

pub fn index_block(services: &Arc<Services>, block: Block<Transaction>) -> Result<()> {
    Ok(())
}

pub fn invalidate_block(_block: Block<Transaction>) -> Result<()> {
    Ok(())
}
