use ain_db::{Column, ColumnName, TypedColumn};

#[derive(Debug)]
pub struct PoolPair;

impl ColumnName for PoolPair {
    const NAME: &'static str = "poolpair";
}

impl Column for PoolPair {
    type Index = (u32, usize);
}

impl TypedColumn for PoolPair {
    type Type = u32;
}
