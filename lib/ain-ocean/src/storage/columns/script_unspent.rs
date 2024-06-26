use crate::model;
use ain_db::{Column, ColumnName, TypedColumn};

#[derive(Debug)]
pub struct ScriptUnspent;

impl ColumnName for ScriptUnspent {
    const NAME: &'static str = "script_unspent";
}

impl Column for ScriptUnspent {
    type Index = model::ScriptUnspentId;
}

impl TypedColumn for ScriptUnspent {
    type Type = model::ScriptUnspent;
}

#[derive(Debug)]
pub struct ScriptUnspentKey;

impl ColumnName for ScriptUnspentKey {
    const NAME: &'static str = "script_unspent_key";
}

impl Column for ScriptUnspentKey {
    type Index = String;
}

impl TypedColumn for ScriptUnspentKey {
    type Type = model::ScriptUnspentId;
}
