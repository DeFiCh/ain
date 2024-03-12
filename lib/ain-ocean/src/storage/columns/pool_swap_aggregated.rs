use ain_db::{Column, ColumnName, TypedColumn};

#[derive(Debug)]
pub struct PoolSwapAggregated;

impl ColumnName for PoolSwapAggregated {
    const NAME: &'static str = "pool_swap_aggregated";
}

impl Column for PoolSwapAggregated {
    type Index = String;
}

impl TypedColumn for PoolSwapAggregated {
    type Type = String;
}
