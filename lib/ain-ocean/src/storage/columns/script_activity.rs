use ain_db::{Column, ColumnName, TypedColumn};
use crate::model;

#[derive(Debug)]
pub struct ScriptActivity;

impl ColumnName for ScriptActivity {
    const NAME: &'static str = "script_activity";
}

impl Column for ScriptActivity {
    type Index = model::ScriptActivityId;
}

impl TypedColumn for ScriptActivity {
    type Type = model::ScriptActivity;
}
