use ain_db::{Column, ColumnName, TypedColumn};

use crate::model;

#[derive(Debug)]
pub struct PoolSwapAggregatedOneDay;

impl ColumnName for PoolSwapAggregatedOneDay {
    const NAME: &'static str = "pool_swap_aggregated_one_day";
}

impl Column for PoolSwapAggregatedOneDay {
    type Index = model::PoolSwapAggregatedId;
}

impl TypedColumn for PoolSwapAggregatedOneDay {
    type Type = model::PoolSwapAggregated;
}

#[derive(Debug)]
pub struct PoolSwapAggregatedOneDayKey;

impl ColumnName for PoolSwapAggregatedOneDayKey {
    const NAME: &'static str = "pool_swap_aggregated_one_day_key";
}

impl Column for PoolSwapAggregatedOneDayKey {
    type Index = model::PoolSwapAggregatedKey;
}

impl TypedColumn for PoolSwapAggregatedOneDayKey {
    type Type = String; // Vec<model::PoolSwapAggregatedId>, std::vector<T> to byte array
}

#[derive(Debug)]
pub struct PoolSwapAggregatedOneHour;

impl ColumnName for PoolSwapAggregatedOneHour {
    const NAME: &'static str = "pool_swap_aggregated_one_hour";
}

impl Column for PoolSwapAggregatedOneHour {
    type Index = model::PoolSwapAggregatedId;
}

impl TypedColumn for PoolSwapAggregatedOneHour {
    type Type = model::PoolSwapAggregated;
}

#[derive(Debug)]
pub struct PoolSwapAggregatedOneHourKey;

impl ColumnName for PoolSwapAggregatedOneHourKey {
    const NAME: &'static str = "pool_swap_aggregated_one_hour_key";
}

impl Column for PoolSwapAggregatedOneHourKey {
    type Index = model::PoolSwapAggregatedKey;
}

impl TypedColumn for PoolSwapAggregatedOneHourKey {
    type Type = String; // Vec<model::PoolSwapAggregatedId>, std::vector<T> to byte array
}
