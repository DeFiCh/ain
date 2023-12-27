use ain_db::{Column, ColumnName, DBError, TypedColumn};

#[derive(Debug)]
pub struct Block;

impl ColumnName for Block {
    const NAME: &'static str = "block";
}

impl Column for Block {
    type Index = String;

    fn key(index: &Self::Index) -> Vec<u8> {
        index.as_bytes().to_vec()
    }

    fn get_key(raw_key: Box<[u8]>) -> Result<Self::Index, DBError> {
        unsafe { Ok(Self::Index::from_utf8_unchecked(raw_key.to_vec())) }
    }
}

impl TypedColumn for Block {
    type Type = String;
}
