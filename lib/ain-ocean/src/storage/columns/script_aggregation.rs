use ain_db::{Column, ColumnName, TypedColumn};

#[derive(Debug)]
pub struct ScriptAggregation;

impl ColumnName for ScriptAggregation {
    const NAME: &'static str = "script_aggregation";
}

impl Column for ScriptAggregation {
    type Index = String;
}

impl TypedColumn for ScriptAggregation {
    type Type = String;
}
