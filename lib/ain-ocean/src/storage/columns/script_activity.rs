use ain_db::{Column, ColumnName, TypedColumn};

#[derive(Debug)]
pub struct ScriptActivity;

impl ColumnName for ScriptActivity {
    const NAME: &'static str = "raw_block";
}

impl Column for ScriptActivity {
    type Index = String;
}

impl TypedColumn for ScriptActivity {
    type Type = String;
}
