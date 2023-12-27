use rocksdb::IteratorMode;

pub mod columns;
pub mod ocean_store;

#[derive(Debug, PartialEq, Clone)]
pub enum SortOrder {
    Ascending,
    Descending,
}

impl<'a> From<SortOrder> for IteratorMode<'a> {
    fn from(sort_order: SortOrder) -> Self {
        match sort_order {
            SortOrder::Ascending => IteratorMode::Start,
            SortOrder::Descending => IteratorMode::End,
        }
    }
}
