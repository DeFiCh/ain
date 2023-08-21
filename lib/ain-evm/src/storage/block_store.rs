use std::fs;
use std::path::Path;
use std::{collections::HashMap, marker::PhantomData, sync::Arc};

use anyhow::format_err;
use ethereum::{BlockAny, TransactionV2};
use primitive_types::{H160, H256, U256};

use super::db::{Column, ColumnName, LedgerColumn, Rocks};
use super::traits::{BlockStorage, FlushableStorage, ReceiptStorage, Rollback, TransactionStorage};
use crate::log::LogIndex;
use crate::receipt::Receipt;
use crate::storage::db::columns;
use crate::storage::traits::LogStorage;
use crate::Result;

#[derive(Debug, Clone)]
pub struct BlockStore(Arc<Rocks>);

impl BlockStore {
    pub fn new(path: &Path) -> Result<Self> {
        let path = path.join("indexes");
        fs::create_dir_all(&path)?;
        let backend = Arc::new(Rocks::open(&path)?);

        Ok(Self(backend))
    }

    pub fn column<C>(&self) -> LedgerColumn<C>
    where
        C: Column + ColumnName,
    {
        LedgerColumn {
            backend: Arc::clone(&self.0),
            column: PhantomData,
        }
    }
}

impl TransactionStorage for BlockStore {
    fn extend_transactions_from_block(&self, block: &BlockAny) -> Result<()> {
        let transactions_cf = self.column::<columns::Transactions>();
        for transaction in &block.transactions {
            transactions_cf.put(&transaction.hash(), transaction)?
        }
        Ok(())
    }

    fn get_transaction_by_hash(&self, hash: &H256) -> Result<Option<TransactionV2>> {
        let transactions_cf = self.column::<columns::Transactions>();
        transactions_cf.get(hash)
    }

    fn get_transaction_by_block_hash_and_index(
        &self,
        block_hash: &H256,
        index: usize,
    ) -> Result<Option<TransactionV2>> {
        let blockmap_cf = self.column::<columns::BlockMap>();
        let blocks_cf = self.column::<columns::Blocks>();

        if let Some(block_number) = blockmap_cf.get(block_hash)? {
            let block = blocks_cf.get(&block_number)?;

            match block {
                Some(block) => Ok(block.transactions.get(index).cloned()),
                None => Ok(None),
            }
        } else {
            Ok(None)
        }
    }

    fn get_transaction_by_block_number_and_index(
        &self,
        block_number: &U256,
        index: usize,
    ) -> Result<Option<TransactionV2>> {
        let blocks_cf = self.column::<columns::Blocks>();
        let block = blocks_cf
            .get(block_number)?
            .ok_or(format_err!("Error fetching block by number"))?;

        Ok(block.transactions.get(index).cloned())
    }

    fn put_transaction(&self, transaction: &TransactionV2) -> Result<()> {
        let transactions_cf = self.column::<columns::Transactions>();
        println!(
            "putting transaction k {:x?} v {:#?}",
            transaction.hash(),
            transaction
        );
        transactions_cf.put(&transaction.hash(), transaction)
    }
}

impl BlockStorage for BlockStore {
    fn get_block_by_number(&self, number: &U256) -> Result<Option<BlockAny>> {
        let blocks_cf = self.column::<columns::Blocks>();
        blocks_cf.get(number)
    }

    fn get_block_by_hash(&self, block_hash: &H256) -> Result<Option<BlockAny>> {
        let blocks_map_cf = self.column::<columns::BlockMap>();
        match blocks_map_cf.get(block_hash) {
            Ok(Some(block_number)) => self.get_block_by_number(&block_number),
            Ok(None) => Ok(None),
            Err(e) => Err(e),
        }
    }

    fn put_block(&self, block: &BlockAny) -> Result<()> {
        self.extend_transactions_from_block(block)?;

        let block_number = block.header.number;
        let hash = block.header.hash();
        let blocks_cf = self.column::<columns::Blocks>();
        let blocks_map_cf = self.column::<columns::BlockMap>();

        blocks_cf.put(&block_number, block)?;
        blocks_map_cf.put(&hash, &block_number)
    }

    fn get_latest_block(&self) -> Result<Option<BlockAny>> {
        let latest_block_cf = self.column::<columns::LatestBlockNumber>();

        match latest_block_cf.get(&()) {
            Ok(Some(block_number)) => self.get_block_by_number(&block_number),
            Ok(None) => Ok(None),
            Err(e) => Err(e),
        }
    }

    fn put_latest_block(&self, block: Option<&BlockAny>) -> Result<()> {
        if let Some(block) = block {
            let latest_block_cf = self.column::<columns::LatestBlockNumber>();
            let block_number = block.header.number;
            latest_block_cf.put(&(), &block_number)?;
        }
        Ok(())
    }
}

impl ReceiptStorage for BlockStore {
    fn get_receipt(&self, tx: &H256) -> Result<Option<Receipt>> {
        let receipts_cf = self.column::<columns::Receipts>();
        receipts_cf.get(tx)
    }

    fn put_receipts(&self, receipts: Vec<Receipt>) -> Result<()> {
        let receipts_cf = self.column::<columns::Receipts>();
        for receipt in receipts {
            receipts_cf.put(&receipt.tx_hash, &receipt)?;
        }
        Ok(())
    }
}

impl LogStorage for BlockStore {
    fn get_logs(&self, block_number: &U256) -> Result<Option<HashMap<H160, Vec<LogIndex>>>> {
        let logs_cf = self.column::<columns::AddressLogsMap>();
        logs_cf.get(block_number)
    }

    fn put_logs(&self, address: H160, logs: Vec<LogIndex>, block_number: U256) -> Result<()> {
        let logs_cf = self.column::<columns::AddressLogsMap>();
        if let Some(mut map) = self.get_logs(&block_number)? {
            map.insert(address, logs);
            logs_cf.put(&block_number, &map)
        } else {
            let map = HashMap::from([(address, logs)]);
            logs_cf.put(&block_number, &map)
        }
    }
}

impl FlushableStorage for BlockStore {
    fn flush(&self) -> Result<()> {
        self.0.flush()
    }
}

impl BlockStore {
    pub fn get_code_by_hash(&self, hash: &H256) -> Result<Option<Vec<u8>>> {
        let code_cf = self.column::<columns::CodeMap>();
        code_cf.get_bytes(hash)
    }

    pub fn put_code(&self, hash: &H256, code: &[u8]) -> Result<()> {
        let code_cf = self.column::<columns::CodeMap>();
        code_cf.put_bytes(hash, code)
    }
}

impl Rollback for BlockStore {
    fn disconnect_latest_block(&self) -> Result<()> {
        if let Some(block) = self.get_latest_block()? {
            println!("disconnecting block number : {:x?}", block.header.number);
            let transactions_cf = self.column::<columns::Transactions>();
            let receipts_cf = self.column::<columns::Receipts>();
            for tx in &block.transactions {
                transactions_cf.delete(&tx.hash())?;
                receipts_cf.delete(&tx.hash())?;
            }

            let blocks_cf = self.column::<columns::Blocks>();
            blocks_cf.delete(&block.header.number)?;

            let blocks_map_cf = self.column::<columns::BlockMap>();
            blocks_map_cf.delete(&block.header.hash())?;

            if let Some(block) = self.get_block_by_hash(&block.header.parent_hash)? {
                let latest_block_cf = self.column::<columns::LatestBlockNumber>();
                latest_block_cf.put(&(), &block.header.number)?;
            }
        }
        Ok(())
    }
}
