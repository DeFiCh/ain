use ain_db::{Column, ColumnName, TypedColumn};
use bitcoin::BlockHash;

#[derive(Debug)]
pub struct RawBlock;

impl ColumnName for RawBlock {
    const NAME: &'static str = "raw_block";
}

impl Column for RawBlock {
    type Index = BlockHash;
}

impl TypedColumn for RawBlock {
    type Type = String;
}
