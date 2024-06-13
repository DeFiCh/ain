use ain_db::{Column, ColumnName, TypedColumn};

#[derive(Debug)]
pub struct ScriptUnspent;

impl ColumnName for ScriptUnspent {
    const NAME: &'static str = "script_unspent";
}

impl Column for ScriptUnspent {
    type Index = String;
}

impl TypedColumn for ScriptUnspent {
    type Type = String;
}
