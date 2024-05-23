use ain_db::{Column, ColumnName, TypedColumn};

use crate::model;

#[derive(Debug)]
pub struct PoolSwapAggregated;

impl ColumnName for PoolSwapAggregated {
    const NAME: &'static str = "pool_swap_aggregated";
}

impl Column for PoolSwapAggregated {
    type Index = model::PoolSwapAggregatedId;
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
}

impl TypedColumn for PoolSwapAggregatedKey {
    type Type = model::PoolSwapAggregatedId;
}
