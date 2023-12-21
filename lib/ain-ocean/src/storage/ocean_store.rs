use std::{fs, marker::PhantomData, path::Path, sync::Arc};

use ain_db::{Column, ColumnName, LedgerColumn, Rocks, TypedColumn};

use super::db::COLUMN_NAMES;
use crate::Result;

#[derive(Debug, Clone)]
pub struct OceanStore(Arc<Rocks>);

impl OceanStore {
    pub fn new(path: &Path) -> Result<Self> {
        let path = path.join("ocean");
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

impl OceanStore {
    pub fn get<C>(&self, key: C::Index) -> Result<Option<C::Type>>
    where
        C: Column + TypedColumn + ColumnName,
    {
        let col = self.column::<C>();
        Ok(col.get(&key)?)
    }

    pub fn put<C>(&self, key: &C::Index, val: &C::Type) -> Result<()>
    where
        C: Column + TypedColumn + ColumnName,
    {
        let col = self.column::<C>();
        let serialized_value = bincode::serialize(val)?;
        Ok(col.put_bytes(key, &serialized_value)?)
    }

    pub fn delete<C>(&self, key: &C::Index) -> Result<()>
    where
        C: Column + TypedColumn + ColumnName,
    {
        let col = self.column::<C>();
        Ok(col.delete(key)?)
    }

    pub fn list<C>(&self, from: Option<C::Index>, limit: usize) -> Vec<(C::Index, C::Type)>
    where
        C: Column + TypedColumn + ColumnName,
    {
        let col = self.column::<C>();
        col.iter(from, limit).collect()
    }
}
