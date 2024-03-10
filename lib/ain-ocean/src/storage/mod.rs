use rocksdb::Direction;

pub mod columns;
pub mod ocean_store;

#[derive(Debug, PartialEq, Clone)]
pub enum SortOrder {
    Ascending,
    Descending,
}

impl From<SortOrder> for Direction {
    fn from(sort_order: SortOrder) -> Self {
        match sort_order {
            SortOrder::Ascending => Direction::Forward,
            SortOrder::Descending => Direction::Reverse,
        }
    }
}
