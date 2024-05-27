use crate::model;
use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, BlockHash};

#[derive(Debug)]
pub struct PoolSwapAggregated;

impl ColumnName for PoolSwapAggregated {
    const NAME: &'static str = "pool_swap_aggregated";
}

impl Column for PoolSwapAggregated {
    type Index = model::PoolSwapAggregatedId;

    fn key(index: &Self::Index) -> Result<Vec<u8>, DBError> {
        let (pool_id, interval, block_hash) = index;
        let mut vec = Vec::with_capacity(40);

        vec.extend_from_slice(&pool_id.to_be_bytes());
        vec.extend_from_slice(&interval.to_be_bytes());
        vec.extend_from_slice(block_hash.as_byte_array());

        Ok(vec)
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        if raw_key.len() != 40 {
            return Err(format_err!("Length of the slice is not 40").into());
        }

        let pool_id = u32::from_be_bytes(
            raw_key[0..4]
                .try_into()
                .map_err(|_| DBError::WrongKeyLength)?,
        );
        let interval = u32::from_be_bytes(
            raw_key[4..8]
                .try_into()
                .map_err(|_| DBError::WrongKeyLength)?,
        );
        let block_hash = BlockHash::from_byte_array(
            raw_key[8..40]
                .try_into()
                .map_err(|_| DBError::WrongKeyLength)?,
        );
        Ok((pool_id, interval, block_hash))
    }
}

impl TypedColumn for PoolSwapAggregated {
    type Type = model::PoolSwapAggregated;
}

#[derive(Debug)]
pub struct PoolSwapAggregatedKey;

impl ColumnName for PoolSwapAggregatedKey {
    const NAME: &'static str = "pool_swap_aggregated_key";
}

impl Column for PoolSwapAggregatedKey {
    type Index = model::PoolSwapAggregatedKey;

    fn key(index: &Self::Index) -> Result<Vec<u8>, DBError> {
        let (pool_id, interval, bucket) = index;
        let mut vec = Vec::with_capacity(16);
        vec.extend_from_slice(&pool_id.to_be_bytes());
        vec.extend_from_slice(&interval.to_be_bytes());
        vec.extend_from_slice(&bucket.to_be_bytes());
        Ok(vec)
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        if raw_key.len() != 16 {
            return Err(format_err!("length of the slice is not 40").into());
        }
        let pool_id = u32::from_be_bytes(
            raw_key[0..4]
                .try_into()
                .map_err(|_| DBError::WrongKeyLength)?,
        );
        let interval = u32::from_be_bytes(
            raw_key[4..8]
                .try_into()
                .map_err(|_| DBError::WrongKeyLength)?,
        );
        let bucket = i64::from_be_bytes(
            raw_key[8..16]
                .try_into()
                .map_err(|_| DBError::WrongKeyLength)?,
        );

        Ok((pool_id, interval, bucket))
    }
}

impl TypedColumn for PoolSwapAggregatedKey {
    type Type = model::PoolSwapAggregatedId;
}
