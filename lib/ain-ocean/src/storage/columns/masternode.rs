use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, Txid};

use crate::model;

#[derive(Debug)]
pub struct Masternode;

impl ColumnName for Masternode {
    const NAME: &'static str = "masternode";
}

impl Column for Masternode {
    type Index = Txid;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_byte_array().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        Self::Index::from_slice(&raw_key).map_err(|_| DBError::ParseKey)
    }
}

impl TypedColumn for Masternode {
    type Type = model::Masternode;
}

// Secondary index by block height and txno
#[derive(Debug)]
pub struct MasternodeByHeight;

impl ColumnName for MasternodeByHeight {
    const NAME: &'static str = "masternode_by_height";
}

impl Column for MasternodeByHeight {
    type Index = (u32, usize);

    fn key(index: &Self::Index) -> Vec<u8> {
        let height_bytes = index.0.to_be_bytes();
        let txno_bytes = index.1.to_be_bytes();

        let height_slice: &[u8] = AsRef::<[u8]>::as_ref(&height_bytes);
        let txno_slice: &[u8] = AsRef::<[u8]>::as_ref(&txno_bytes);

        [height_slice, txno_slice].concat()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        if raw_key.len() != 12 {
            return Err(DBError::Custom(format_err!("Wrong key length")));
        }

        let height_bytes = <[u8; 4]>::try_from(&raw_key[..4])
            .map_err(|_| DBError::Custom(format_err!("Invalid height bytes")))?;
        let txno_bytes = <[u8; 8]>::try_from(&raw_key[4..])
            .map_err(|_| DBError::Custom(format_err!("Invalid txno bytes")))?;

        let height = u32::from_be_bytes(height_bytes);
        let txno = usize::from_be_bytes(txno_bytes);

        Ok((height, txno))
    }
}

impl TypedColumn for MasternodeByHeight {
    type Type = String;
}
