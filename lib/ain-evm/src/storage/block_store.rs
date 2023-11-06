use std::{
    collections::HashMap, fmt::Write, fs, marker::PhantomData, path::Path, str::FromStr, sync::Arc,
};

use anyhow::format_err;
use ethereum::{BlockAny, TransactionV2};
use ethereum_types::{H160, H256, U256};
use log::debug;
use serde::{Deserialize, Serialize};

use super::{
    db::{Column, ColumnName, LedgerColumn, Rocks, TypedColumn},
    migration::{Migration, MigrationV1},
    traits::{BlockStorage, FlushableStorage, ReceiptStorage, Rollback, TransactionStorage},
};
use crate::{
    log::LogIndex,
    receipt::Receipt,
    storage::{db::columns, traits::LogStorage},
    Result,
};

#[derive(Debug, Clone)]
pub struct BlockStore(Arc<Rocks>);

impl BlockStore {
    pub fn new(path: &Path) -> Result<Self> {
        let path = path.join("indexes");
        fs::create_dir_all(&path)?;
        let backend = Arc::new(Rocks::open(&path)?);
        let store = Self(backend);
        store.startup()?;
        Ok(store)
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

/// This implementation block includes a versioning system for database migrations.
/// It ensures that the database schema is up-to-date with the node's expectations
/// by applying necessary migrations upon startup. The `CURRENT_VERSION` constant reflects
/// the latest version of the database schemas expected by the node.
///
/// The `migrate` method sequentially applies any required migrations based on the current
/// database version. The version information is stored in the `metadata`` column family
/// within the RocksDB instance.
///
/// Migrations are defined as implementations of the `Migration` trait and are executed
/// in order of their version number.
///
/// The `startup` method initializes the migration process as part of the startup flow
/// and should be called on BlockStore initialization.
///
impl BlockStore {
    const VERSION_KEY: &'static str = "version";
    const CURRENT_VERSION: u32 = 1;

    /// Sets the version number in the database to the specified `version`.
    ///
    /// This operation is atomic and flushes the updated version to disk immediately
    /// to maintain consistency even in the event of a failure or shutdown following the update.
    fn set_version(&self, version: u32) -> Result<()> {
        let handle = self.0.cf_handle(columns::Metadata::NAME);
        self.0
            .put_cf(handle, Self::VERSION_KEY.as_bytes(), &version.to_be_bytes())?;
        self.0.flush()
    }

    /// Retrieves the current version number from the database.
    fn get_version(&self) -> Result<u32> {
        let handle = self.0.cf_handle(columns::Metadata::NAME);
        let version = self
            .0
            .get_cf(handle, Self::VERSION_KEY.as_bytes())?
            .ok_or(format_err!("Missing version"))
            .and_then(|bytes| {
                bytes
                    .as_slice()
                    .try_into()
                    .map_err(|e| format_err!("{e}"))
                    .map(u32::from_be_bytes)
            })?;
        Ok(version)
    }

    /// Executes the migration process.
    ///
    /// It checks for the current database version and applies all the necessary migrations
    /// that have a version number greater than the current one. After all migrations are
    /// applied, sets the database version to `CURRENT_VERSION`.
    fn migrate(&self) -> Result<()> {
        let current_version = self.get_version().unwrap_or(0);
        let mut migrations: Vec<Box<dyn Migration>> = vec![Box::new(MigrationV1)];
        migrations.sort_by_key(|a| a.version());

        // Assert that migrations vec has properly been populated
        if migrations.len() != Self::CURRENT_VERSION as usize {
            return Err(format_err!("Missing migration").into());
        }

        for migration in migrations {
            if current_version < migration.version() {
                migration.migrate(self)?;
                self.set_version(migration.version())?;
            }
        }

        self.set_version(Self::CURRENT_VERSION)
    }

    /// Startup routine to ensure the database schema is up-to-date.
    ///
    /// This should be called after BlockStore initialization.
    fn startup(&self) -> Result<()> {
        self.migrate()
    }
}

impl TransactionStorage for BlockStore {
    fn put_transactions_from_block(&self, block: &BlockAny) -> Result<()> {
        let transactions_cf = self.column::<columns::Transactions>();
        let block_hash = block.header.hash();
        for (index, transaction) in block.transactions.iter().enumerate() {
            transactions_cf.put(&transaction.hash(), &(block_hash, index))?
        }
        Ok(())
    }

    fn get_transaction_by_hash(&self, hash: &H256) -> Result<Option<TransactionV2>> {
        let transactions_cf = self.column::<columns::Transactions>();
        transactions_cf.get(hash).and_then(|opt| {
            opt.map_or(Ok(None), |(hash, idx)| {
                self.get_transaction_by_block_hash_and_index(&hash, idx)
            })
        })
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
        self.put_transactions_from_block(block)?;

        let block_number = block.header.number;
        let hash = block.header.hash();
        let blocks_cf = self.column::<columns::Blocks>();
        let blocks_map_cf = self.column::<columns::BlockMap>();

        blocks_cf.put(&block_number, block)?;
        blocks_map_cf.put(&hash, &block_number)
    }

    fn get_latest_block(&self) -> Result<Option<BlockAny>> {
        let latest_block_cf = self.column::<columns::LatestBlockNumber>();

        match latest_block_cf.get(&"") {
            Ok(Some(block_number)) => self.get_block_by_number(&block_number),
            Ok(None) => Ok(None),
            Err(e) => Err(e),
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
    pub fn get_code_by_hash(&self, address: H160, hash: &H256) -> Result<Option<Vec<u8>>> {
        let address_codes_cf = self.column::<columns::AddressCodeMap>();
        address_codes_cf.get_bytes(&(address, *hash))
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
        block_deployed_codes_cf.put(&(block_number, address), hash)
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
                block_deployed_codes_cf.iter(Some((block.header.number, H160::zero())), None);

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

#[derive(Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum DumpArg {
    All,
    Blocks,
    Txs,
    Receipts,
    BlockMap,
    Logs,
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
        for arg in &[
            DumpArg::Blocks,
            DumpArg::Txs,
            DumpArg::Receipts,
            DumpArg::BlockMap,
            DumpArg::Logs,
        ] {
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
        for (k, v) in self.column::<C>().iter(from, Some(limit)) {
            writeln!(&mut out, "{:?}: {:#?}", k, v)
                .map_err(|_| format_err!("failed to write to stream"))?;
        }
        Ok(out)
    }
}
