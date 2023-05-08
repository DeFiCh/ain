use std::convert::From;
use std::mem::size_of_val;

use ethereum::BlockAny;

use crate::codegen::types::EthBlockInfo;
use crate::utils::{format_h256, format_u256};

impl From<BlockAny> for EthBlockInfo {
    fn from(block: BlockAny) -> Self {
        EthBlockInfo {
            block_number: format!("{:#x}", block.header.number),
            hash: format_h256(block.header.hash()),
            parent_hash: format_h256(block.header.parent_hash),
            nonce: format!("{:#x}", block.header.nonce),
            sha3_uncles: format_h256(block.header.ommers_hash),
            logs_bloom: format!("{:#x}", block.header.logs_bloom),
            transactions_root: format_h256(block.header.transactions_root),
            state_root: format_h256(block.header.state_root),
            receipt_root: format_h256(block.header.receipts_root),
            miner: format!("{:#x}", block.header.beneficiary),
            difficulty: format!("{:#x}", block.header.difficulty),
            total_difficulty: format_u256(block.header.difficulty),
            extra_data: format!("{:#x?}", block.header.extra_data.to_ascii_lowercase()),
            size: format!("{:#x}", size_of_val(&block)),
            gas_limit: format_u256(block.header.gas_limit),
            gas_used: format_u256(block.header.gas_used),
            timestamps: format!("0x{:x}", block.header.timestamp),
            transactions: block
                .transactions
                .iter()
                .map(|x| x.hash().to_string())
                .collect::<Vec<String>>(),
            uncles: block
                .ommers
                .iter()
                .map(|x| x.hash().to_string())
                .collect::<Vec<String>>(),
        }
    }
}
