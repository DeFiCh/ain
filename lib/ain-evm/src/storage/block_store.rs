use std::{
    collections::HashMap, fmt::Write, fs, marker::PhantomData, path::Path, str::FromStr, sync::Arc,
};

use ain_db::{Column, ColumnName, LedgerColumn, Rocks, TypedColumn};
use anyhow::format_err;
use ethereum::{BlockAny, TransactionV2};
use ethereum_types::{H160, H256, U256};
use log::debug;

use super::{
    db::COLUMN_NAMES,
    traits::{BlockStorage, FlushableStorage, ReceiptStorage, Rollback, TransactionStorage},
};
use crate::{
    log::LogIndex,
    receipt::Receipt,
    storage::{db::columns, traits::LogStorage},
    EVMError, Result,
};

#[derive(Debug, Clone)]
pub struct BlockStore(Arc<Rocks>);

impl BlockStore {
    pub fn new(path: &Path) -> Result<Self> {
        let path = path.join("indexes");
        fs::create_dir_all(&path)?;
        let backend = Arc::new(Rocks::open(&path, &COLUMN_NAMES)?);

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
        Ok(transactions_cf.get(hash)?)
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
        Ok(transactions_cf.put(&transaction.hash(), transaction)?)
    }
}

impl BlockStorage for BlockStore {
    fn get_block_by_number(&self, number: &U256) -> Result<Option<BlockAny>> {
        let blocks_cf = self.column::<columns::Blocks>();
        Ok(blocks_cf.get(number)?)
    }

    fn get_block_by_hash(&self, block_hash: &H256) -> Result<Option<BlockAny>> {
        let blocks_map_cf = self.column::<columns::BlockMap>();
        match blocks_map_cf.get(block_hash)? {
            Some(block_number) => self.get_block_by_number(&block_number),
            None => Ok(None),
        }
    }

    fn put_block(&self, block: &BlockAny) -> Result<()> {
        self.extend_transactions_from_block(block)?;
        let block_number = block.header.number;
        let hash = block.header.hash();
        let blocks_cf = self.column::<columns::Blocks>();
        let blocks_map_cf = self.column::<columns::BlockMap>();

        blocks_cf.put(&block_number, block)?;
        Ok(blocks_map_cf.put(&hash, &block_number)?)
    }

    fn get_latest_block(&self) -> Result<Option<BlockAny>> {
        let latest_block_cf = self.column::<columns::LatestBlockNumber>();

        match latest_block_cf.get(&"")? {
            Some(block_number) => self.get_block_by_number(&block_number),
            None => Ok(None),
        }
    }

    fn put_latest_block(&self, block: Option<&BlockAny>) -> Result<()> {
        if let Some(block) = block {
            let latest_block_cf = self.column::<columns::LatestBlockNumber>();
            let block_number = block.header.number;
            latest_block_cf.put(&"latest_block", &block_number)?;
        }
        Ok(())
    }
}

impl ReceiptStorage for BlockStore {
    fn get_receipt(&self, tx: &H256) -> Result<Option<Receipt>> {
        let receipts_cf = self.column::<columns::Receipts>();
        Ok(receipts_cf.get(tx)?)
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
        Ok(logs_cf.get(block_number)?)
    }

    fn put_logs(&self, address: H160, logs: Vec<LogIndex>, block_number: U256) -> Result<()> {
        let logs_cf = self.column::<columns::AddressLogsMap>();
        if let Some(mut map) = self.get_logs(&block_number)? {
            map.insert(address, logs);
            Ok(logs_cf.put(&block_number, &map)?)
        } else {
            let map = HashMap::from([(address, logs)]);
            Ok(logs_cf.put(&block_number, &map)?)
        }
    }
}

impl FlushableStorage for BlockStore {
    fn flush(&self) -> Result<()> {
        Ok(self.0.flush()?)
    }
}

impl BlockStore {
    pub fn get_code_by_hash(&self, address: H160, hash: &H256) -> Result<Option<Vec<u8>>> {
        let address_codes_cf = self.column::<columns::AddressCodeMap>();
        Ok(address_codes_cf.get_bytes(&(address, *hash))?)
    }

    pub fn put_code(
        &self,
        block_number: U256,
        address: H160,
        hash: &H256,
        code: &[u8],
    ) -> Result<()> {
        let address_codes_cf = self.column::<columns::AddressCodeMap>();
        address_codes_cf.put_bytes(&(address, *hash), code)?;

        let block_deployed_codes_cf = self.column::<columns::BlockDeployedCodeHashes>();
        Ok(block_deployed_codes_cf.put(&(block_number, address), hash)?)
    }
}

impl Rollback for BlockStore {
    fn disconnect_latest_block(&self) -> Result<()> {
        if let Some(block) = self.get_latest_block()? {
            debug!(
                "[disconnect_latest_block] disconnecting block number : {:x?}",
                block.header.number
            );
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
                latest_block_cf.put(&"latest_block", &block.header.number)?;
            }

            let logs_cf = self.column::<columns::AddressLogsMap>();
            logs_cf.delete(&block.header.number)?;

            let block_deployed_codes_cf = self.column::<columns::BlockDeployedCodeHashes>();
            let mut iter =
                block_deployed_codes_cf.iter(Some((block.header.number, H160::zero())), usize::MAX);

            let address_codes_cf = self.column::<columns::AddressCodeMap>();
            for ((block_number, address), hash) in &mut iter {
                if block_number == block.header.number {
                    address_codes_cf.delete(&(address, hash))?;
                    block_deployed_codes_cf.delete(&(block.header.number, address))?;
                } else {
                    break;
                }
            }
        }
        Ok(())
    }
}

pub enum DumpArg {
    All,
    Blocks,
    Txs,
    Receipts,
    BlockMap,
    Logs,
}

impl TryFrom<String> for DumpArg {
    type Error = EVMError;

    fn try_from(arg: String) -> Result<Self> {
        match arg.as_str() {
            "all" => Ok(DumpArg::All),
            "blocks" => Ok(DumpArg::Blocks),
            "txs" => Ok(DumpArg::Txs),
            "receipts" => Ok(DumpArg::Receipts),
            "blockmap" => Ok(DumpArg::BlockMap),
            "logs" => Ok(DumpArg::Logs),
            _ => Err(format_err!("Invalid dump arg").into()),
        }
    }
}

impl BlockStore {
    pub fn dump(&self, arg: &DumpArg, from: Option<&str>, limit: usize) -> Result<String> {
        let s_to_u256 = |s| {
            U256::from_str_radix(s, 10)
                .or(U256::from_str_radix(s, 16))
                .unwrap_or_else(|_| U256::zero())
        };
        let s_to_h256 = |s: &str| H256::from_str(s).unwrap_or(H256::zero());

        match arg {
            DumpArg::All => self.dump_all(limit),
            DumpArg::Blocks => self.dump_column(columns::Blocks, from.map(s_to_u256), limit),
            DumpArg::Txs => self.dump_column(columns::Transactions, from.map(s_to_h256), limit),
            DumpArg::Receipts => self.dump_column(columns::Receipts, from.map(s_to_h256), limit),
            DumpArg::BlockMap => self.dump_column(columns::BlockMap, from.map(s_to_h256), limit),
            DumpArg::Logs => self.dump_column(columns::AddressLogsMap, from.map(s_to_u256), limit),
        }
    }

    fn dump_all(&self, limit: usize) -> Result<String> {
        let mut out = String::new();
        let response_max_size = usize::try_from(ain_cpp_imports::get_max_response_byte_size())
            .map_err(|_| format_err!("failed to convert response size limit to usize"))?;

        for arg in &[
            DumpArg::Blocks,
            DumpArg::Txs,
            DumpArg::Receipts,
            DumpArg::BlockMap,
            DumpArg::Logs,
        ] {
            if out.len() > response_max_size {
                return Err(format_err!("exceed response max size limit").into());
            }
            writeln!(&mut out, "{}", self.dump(arg, None, limit)?)
                .map_err(|_| format_err!("failed to write to stream"))?;
        }
        Ok(out)
    }

    fn dump_column<C>(&self, _: C, from: Option<C::Index>, limit: usize) -> Result<String>
    where
        C: TypedColumn + ColumnName,
    {
        let mut out = format!("{}\n", C::NAME);
        let response_max_size = usize::try_from(ain_cpp_imports::get_max_response_byte_size())
            .map_err(|_| format_err!("failed to convert response size limit to usize"))?;

        for (k, v) in self.column::<C>().iter(from, limit) {
            if out.len() > response_max_size {
                return Err(format_err!("exceed response max size limit").into());
            }
            writeln!(&mut out, "{:?}: {:#?}", k, v)
                .map_err(|_| format_err!("failed to write to stream"))?;
        }
        Ok(out)
    }
}
