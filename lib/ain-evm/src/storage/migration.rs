//! # Migrations Module
//!
//! This module provides the necessary abstractions and implementations to perform
//! data migrations within `BlockStore``.
//!
//! ## Overview
//!
//! The `Migration` trait defines the core requirements for any migration: a target version
//! and a migration method. Each concrete implementation of this trait corresponds to a specific
//! migration version and contains the logic to update the `BlockStore` to that version.
//!
//! Implementations are expected to be idempotent to ensure safety across re-runs, which might occur
//! in scenarios like recovery from partial migrations or failures.
//!
//! Wherever possible, migrations should leverage parallel processing with `rayon`
//! to improve performance and end-user UX
//!
//! ## Usage
//!
//! Migrations are to be executed during the startup phase of the node, ensuring the `BlockStore`
//! reflects the current expected state of the schemas.

use rayon::prelude::*;

use super::{block_store::BlockStore, db::columns};
use crate::Result;

/// The `Migration` trait, which all migrations must implement.
pub trait Migration {
    /// Returns the target version number for the migration.
    fn version(&self) -> u32;
    /// Contains the logic to migrate the given `BlockStore` to the target version.
    ///
    /// # Idempotency
    /// Implementations should ensure that the migration can be run multiple times
    /// without causing additional changes after the first successful run.
    fn migrate(&self, store: &BlockStore) -> Result<()>;
}

/// Migration for version 1.
/// Context:
/// Release v4.0.1
/// Remove duplicate TransactionV2 entries in storage and store block hash and tx index instead.
pub struct MigrationV1;

impl Migration for MigrationV1 {
    fn version(&self) -> u32 {
        1
    }

    fn migrate(&self, store: &BlockStore) -> Result<()> {
        self.migrate_transactions(store)?;
        Ok(())
    }
}

impl MigrationV1 {
    /// Migrates transactions to be associated with their respective block hashes and indexes.
    fn migrate_transactions(&self, store: &BlockStore) -> Result<()> {
        let transactions_cf = store.column::<columns::Transactions>();
        let blocks_cf = store.column::<columns::Blocks>();

        blocks_cf
            .iter(None, None)
            .par_bridge()
            .try_for_each(|(_, block)| {
                let block_hash = block.header.hash();
                block
                    .transactions
                    .par_iter()
                    .enumerate()
                    .try_for_each(|(index, transaction)| {
                        transactions_cf.put(&transaction.hash(), &(block_hash, index))
                    })
            })?;

        Ok(())
    }
}
