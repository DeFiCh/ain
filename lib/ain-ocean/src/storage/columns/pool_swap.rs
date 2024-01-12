use ain_db::{Column, ColumnName, TypedColumn};

use crate::model;

#[derive(Debug)]
pub struct PoolSwap;

impl ColumnName for PoolSwap {
    const NAME: &'static str = "pool_swap";
}

impl Column for PoolSwap {
    type Index = model::PoolSwapKey;
}

impl TypedColumn for PoolSwap {
    type Type = model::PoolSwap;
}
