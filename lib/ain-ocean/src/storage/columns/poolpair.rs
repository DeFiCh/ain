use ain_db::{Column, ColumnName, TypedColumn};

#[derive(Debug)]
pub struct PoolPairByHeight;

impl ColumnName for PoolPairByHeight {
    const NAME: &'static str = "poolpair";
}

impl Column for PoolPairByHeight {
    type Index = (u32, usize);
}

impl TypedColumn for PoolPairByHeight {
    type Type = (u32, u32, u32);
}

#[derive(Debug)]
pub struct PoolPair;

impl ColumnName for PoolPair {
    const NAME: &'static str = "poolpair";
}

impl Column for PoolPair {
    type Index = (u32, u32);
}

impl TypedColumn for PoolPair {
    type Type = u32;
}
