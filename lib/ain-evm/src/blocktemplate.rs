use ethereum::{Block, ReceiptV3, TransactionV2};
use ethereum_types::{Bloom, H160, U256};

use crate::{
    backend::{EVMBackend, Vicinity},
    core::XHash,
    evm::{BlockContext, ExecTxState},
    receipt::Receipt,
    transaction::SignedTx,
};

type Result<T> = std::result::Result<T, BlockTemplateError>;

pub type ReceiptAndOptionalContractAddress = (ReceiptV3, Option<H160>);

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TemplateTxItem {
    pub tx: Box<SignedTx>,
    pub tx_hash: XHash,
    pub gas_fees: U256,
    pub gas_used: U256,
    pub logs_bloom: Bloom,
    pub receipt_v3: ReceiptAndOptionalContractAddress,
}

impl TemplateTxItem {
    pub fn new_system_tx(
        tx: Box<SignedTx>,
        receipt_v3: ReceiptAndOptionalContractAddress,
        logs_bloom: Bloom,
    ) -> Self {
        TemplateTxItem {
            tx,
            tx_hash: Default::default(),
            gas_fees: U256::zero(),
            gas_used: U256::zero(),
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
/// 7. EVM backend
///
/// The template is used to construct a valid EVM block.
///
pub struct BlockTemplate {
    pub transactions: Vec<TemplateTxItem>,
    pub block_data: Option<BlockData>,
    pub total_gas_used: U256,
    pub vicinity: Vicinity,
    pub ctx: BlockContext,
    pub backend: EVMBackend,
}

impl BlockTemplate {
    pub fn new(vicinity: Vicinity, ctx: BlockContext, backend: EVMBackend) -> Self {
        Self {
            transactions: Vec::new(),
            block_data: None,
            total_gas_used: U256::zero(),
            vicinity,
            ctx,
            backend,
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
            gas_fees: tx_update.gas_fees,
            gas_used: tx_update.gas_used,
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
            })?;
            self.backend.reset_from_tx(index);
        }

        Ok(removed_txs)
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

    pub fn is_genesis_block(&self) -> bool {
        self.get_block_number() == U256::zero()
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
