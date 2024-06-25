use crate::model;
use ain_db::{Column, ColumnName, TypedColumn};

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

#[derive(Debug)]
pub struct ScriptActivityKey;

impl ColumnName for ScriptActivityKey {
    const NAME: &'static str = "script_activity_key";
}

impl Column for ScriptActivityKey {
    type Index = String;
}

impl TypedColumn for ScriptActivityKey {
    type Type = model::ScriptActivityId;
}

