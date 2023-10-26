use ethereum::{Block, ReceiptV3, TransactionV2};
use ethereum_types::{Bloom, H160, H256, U256};

use crate::{
    backend::Vicinity, core::XHash, evm::ExecTxState, receipt::Receipt, transaction::SignedTx,
};

type Result<T> = std::result::Result<T, BlockTemplateError>;

pub type ReceiptAndOptionalContractAddress = (ReceiptV3, Option<H160>);

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TemplateTxItem {
    pub tx: Box<SignedTx>,
    pub tx_hash: XHash,
    pub gas_used: U256,
    pub gas_fees: U256,
    pub state_root: H256,
    pub logs_bloom: Bloom,
    pub receipt_v3: ReceiptAndOptionalContractAddress,
}

impl TemplateTxItem {
    pub fn new_system_tx(
        tx: Box<SignedTx>,
        receipt_v3: ReceiptAndOptionalContractAddress,
        state_root: H256,
        logs_bloom: Bloom,
    ) -> Self {
        TemplateTxItem {
            tx,
            tx_hash: Default::default(),
            gas_used: U256::zero(),
            gas_fees: U256::zero(),
            state_root,
            logs_bloom,
            receipt_v3,
        }
    }
}

#[derive(Clone, Debug)]
pub struct BlockData {
    pub block: Block<TransactionV2>,
    pub receipts: Vec<Receipt>,
}

/// The `BlockTemplateData` contains:
/// 1. Validated transactions in the block template
/// 2. Block data
/// 3. Total gas used by all in the block template
/// 4. Backend vicinity
/// 5. Block template timestamp
/// 6. DVM block number
/// 7. Initial state root
///
/// The template is used to construct a valid EVM block.
///
#[derive(Clone, Debug, Default)]
pub struct BlockTemplate {
    pub transactions: Vec<TemplateTxItem>,
    pub block_data: Option<BlockData>,
    pub total_gas_used: U256,
    pub vicinity: Vicinity,
    pub parent_hash: H256,
    pub dvm_block: u64,
    pub timestamp: u64,
    pub initial_state_root: H256,
}

impl BlockTemplate {
    pub fn new(
        vicinity: Vicinity,
        parent_hash: H256,
        dvm_block: u64,
        timestamp: u64,
        initial_state_root: H256,
    ) -> Self {
        Self {
            transactions: Vec::new(),
            block_data: None,
            total_gas_used: U256::zero(),
            vicinity,
            parent_hash,
            dvm_block,
            timestamp,
            initial_state_root,
        }
    }

    pub fn add_tx(&mut self, tx_update: ExecTxState, tx_hash: XHash) -> Result<()> {
        self.total_gas_used = self
            .total_gas_used
            .checked_add(tx_update.gas_used)
            .ok_or(BlockTemplateError::ValueOverflow)?;

        self.transactions.push(TemplateTxItem {
            tx: tx_update.tx,
            tx_hash,
            gas_used: tx_update.gas_used,
            gas_fees: tx_update.gas_fees,
            state_root: tx_update.state_root,
            logs_bloom: tx_update.logs_bloom,
            receipt_v3: tx_update.receipt,
        });
        Ok(())
    }

    pub fn remove_txs_above_hash(&mut self, target_hash: XHash) -> Result<Vec<XHash>> {
        let mut removed_txs = Vec::new();

        if let Some(index) = self
            .transactions
            .iter()
            .position(|item| item.tx_hash == target_hash)
        {
            removed_txs = self
                .transactions
                .drain(index..)
                .map(|tx_item| tx_item.tx_hash)
                .collect();

            self.total_gas_used = self.transactions.iter().try_fold(U256::zero(), |acc, tx| {
                acc.checked_add(tx.gas_used)
                    .ok_or(BlockTemplateError::ValueOverflow)
            })?
        }

        Ok(removed_txs)
    }

    pub fn get_state_root_from_native_hash(&self, hash: XHash) -> Option<H256> {
        self.transactions
            .iter()
            .find(|tx_item| tx_item.tx_hash == hash)
            .map(|tx_item| tx_item.state_root)
    }

    pub fn get_latest_state_root(&self) -> H256 {
        self.transactions
            .last()
            .map_or(self.initial_state_root, |tx_item| tx_item.state_root)
    }

    pub fn get_latest_logs_bloom(&self) -> Bloom {
        self.transactions
            .last()
            .map_or(Bloom::default(), |tx_item| tx_item.logs_bloom)
    }

    pub fn get_block_base_fee_per_gas(&self) -> U256 {
        self.vicinity.block_base_fee_per_gas
    }

    pub fn get_block_number(&self) -> U256 {
        self.vicinity.block_number
    }
}

#[derive(Debug)]
pub enum BlockTemplateError {
    NoSuchTemplate,
    ValueOverflow,
}

impl std::fmt::Display for BlockTemplateError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            BlockTemplateError::NoSuchTemplate => write!(f, "No block template for this id"),
            BlockTemplateError::ValueOverflow => {
                write!(f, "Value overflow when updating block template")
            }
        }
    }
}

impl std::error::Error for BlockTemplateError {}
