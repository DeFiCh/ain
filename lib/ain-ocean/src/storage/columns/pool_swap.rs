use ain_db::{Column, ColumnName, TypedColumn};

#[derive(Debug)]
pub struct PoolSwap;

impl ColumnName for PoolSwap {
    const NAME: &'static str = "pool_swap";
}

impl Column for PoolSwap {
    type Index = String;
}

impl TypedColumn for PoolSwap {
    type Type = String;
}
