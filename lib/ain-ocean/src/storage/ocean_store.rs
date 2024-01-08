use std::{fs, marker::PhantomData, path::Path, sync::Arc};

use ain_db::{Column, ColumnName, LedgerColumn, Rocks};

use super::columns::COLUMN_NAMES;
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
