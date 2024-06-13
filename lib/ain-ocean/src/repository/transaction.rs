use std::sync::Arc;

use ain_db::LedgerColumn;
use ain_macros::Repository;
use bitcoin::{BlockHash, Txid};

use super::{InitialKeyProvider, RepositoryOps, SecondaryIndex};
use crate::{
    model::{Transaction, TransactionByBlockHashKey},
    storage::{columns, ocean_store::OceanStore},
    Error, Result,
};

#[derive(Repository)]
#[repository(K = "Txid", V = "Transaction")]
pub struct TransactionRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::Transaction>,
}

#[derive(Repository)]
#[repository(K = "TransactionByBlockHashKey", V = "Txid")]
pub struct TransactionByBlockHashRepository {
    pub store: Arc<OceanStore>,
    col: LedgerColumn<columns::TransactionByBlockHash>,
}

impl InitialKeyProvider<TransactionByBlockHashKey, Txid> for TransactionByBlockHashRepository {
    type PartialKey = BlockHash;

    fn initial_key(pk: Self::PartialKey) -> TransactionByBlockHashKey {
        (pk, 0)
    }
}

impl SecondaryIndex<TransactionByBlockHashKey, Txid> for TransactionByBlockHashRepository {
    type Value = Transaction;

    fn retrieve_primary_value(&self, el: Self::ListItem) -> Result<Self::Value> {
        let (_, id) = el?;
        let col = self.store.column::<columns::Transaction>();
        let tx = col.get(&id)?.ok_or(Error::SecondaryIndex)?;
        Ok(tx)
    }
}
