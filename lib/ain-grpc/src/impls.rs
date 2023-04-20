use std::convert::From;
use std::mem::size_of_val;

use ain_evm::transaction::{SignedTx, TransactionError};
use ethereum::{BlockAny, TransactionV2};
use primitive_types::{H160, H256, U256};

use crate::codegen::types::{EthBlockInfo, EthTransactionInfo};

fn format_hash(hash: H256) -> String {
    format!("{:#x}", hash)
}

fn format_address(hash: H160) -> String {
    format!("{:#x}", hash)
}

fn format_number(number: U256) -> String {
    format!("{:#x}", number)
}

impl From<BlockAny> for EthBlockInfo {
    fn from(block: BlockAny) -> Self {
        EthBlockInfo {
            block_number: format!("{:#x}", block.header.number),
            hash: format_hash(block.header.hash()),
            parent_hash: format_hash(block.header.parent_hash),
            nonce: format!("{:#x}", block.header.nonce),
            sha3_uncles: format_hash(block.header.ommers_hash),
            logs_bloom: format!("{:#x}", block.header.logs_bloom),
            transactions_root: format_hash(block.header.transactions_root),
            state_root: format_hash(block.header.state_root),
            receipt_root: format_hash(block.header.receipts_root),
            miner: format!("{:#x}", block.header.beneficiary),
            difficulty: format!("{:#x}", block.header.difficulty),
            total_difficulty: format_number(block.header.difficulty),
            extra_data: format!("{:#x?}", block.header.extra_data.to_ascii_lowercase()),
            size: format!("{:#x}", size_of_val(&block)),
            gas_limit: format_number(block.header.gas_limit),
            gas_used: format_number(block.header.gas_used),
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

impl TryFrom<TransactionV2> for EthTransactionInfo {
    type Error = TransactionError;

    fn try_from(tx: TransactionV2) -> Result<Self, Self::Error> {
        let signed_tx: SignedTx = tx.try_into()?;

        Ok(EthTransactionInfo {
            from: format_address(signed_tx.sender),
            to: signed_tx.to().map(format_address),
            gas: signed_tx.gas_limit().as_u64(),
            price: signed_tx.gas_price().to_string(),
            value: signed_tx.value().to_string(),
            data: hex::encode(signed_tx.data()),
            nonce: signed_tx.nonce().to_string(),
        })
    }
}
