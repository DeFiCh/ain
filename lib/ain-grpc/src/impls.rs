use std::convert::From;
use std::mem::size_of_val;

use ain_evm::transaction::{SignedTx, TransactionError};
use ethereum::{BlockAny, TransactionV2};
use primitive_types::{H160, H256, U256};

use crate::codegen::types::{EthBlockInfo, EthPendingTransactionInfo, EthTransactionInfo};

fn format_hash(hash: H256) -> String {
    format!("{hash:#x}")
}

fn format_address(hash: H160) -> String {
    format!("{hash:#x}")
}

fn format_number(number: U256) -> String {
    format!("{number:#x}")
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

impl TryFrom<&str> for EthPendingTransactionInfo {
    type Error = TransactionError;

    fn try_from(raw_tx: &str) -> Result<Self, Self::Error> {
        let signed_tx: SignedTx = raw_tx.try_into()?;

        let to = if let Some(signed_to) = signed_tx.to() {
            format_address(signed_to)
        } else {
            String::from("null")
        };

        let input = if signed_tx.data().is_empty() {
            String::from("0x0")
        } else {
            format!("0x{}", hex::encode(signed_tx.data()))
        };

        let pending_transaction = EthPendingTransactionInfo {
            hash: format_hash(signed_tx.transaction.hash()),
            nonce: format_number(signed_tx.nonce()),
            block_hash: String::from(
                "0x0000000000000000000000000000000000000000000000000000000000000000",
            ),
            block_number: String::from("null"),
            transaction_index: String::from("0x0"),
            from: format_address(signed_tx.sender),
            to,
            value: format_number(signed_tx.value()),
            gas: format_number(signed_tx.gas_limit()),
            gas_price: format_number(signed_tx.gas_price()),
            input,
            v: format!("0x{:x}", signed_tx.v()),
            r: format_hash(signed_tx.r()),
            s: format_hash(signed_tx.s()),
        };

        Ok(pending_transaction)
    }
}
