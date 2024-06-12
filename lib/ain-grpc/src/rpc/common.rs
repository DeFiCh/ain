use std::sync::Arc;

use ain_evm::storage::{traits::BlockStorage, Storage};
use ethereum::BlockAny;
use ethereum_types::U256;
use jsonrpsee::core::RpcResult;

use crate::{block::BlockNumber, errors::RPCError};

pub fn get_block(storage: &Arc<Storage>, block_number: Option<BlockNumber>) -> RpcResult<BlockAny> {
    match block_number.unwrap_or(BlockNumber::Latest) {
        BlockNumber::Hash { hash, .. } => storage.get_block_by_hash(&hash),
        BlockNumber::Num(n) => storage.get_block_by_number(&U256::from(n)),
        BlockNumber::Earliest => storage.get_block_by_number(&U256::zero()),
        BlockNumber::Safe | BlockNumber::Finalized => {
            storage.get_latest_block().and_then(|block| {
                block.map_or(Ok(None), |block| {
                    let finality_count = ain_cpp_imports::get_attribute_values(None).finality_count;

                    block
                        .header
                        .number
                        .checked_sub(U256::from(finality_count))
                        .map_or(Ok(None), |safe_block_number| {
                            storage.get_block_by_number(&safe_block_number)
                        })
                })
            })
        }
        // BlockNumber::Pending => todo!(),
        _ => storage.get_latest_block(),
    }
    .map_err(RPCError::EvmError)?
    .ok_or(RPCError::BlockNotFound.into())
}
