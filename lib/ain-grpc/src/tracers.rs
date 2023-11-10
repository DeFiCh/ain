use serde::de::Error;
use serde::{Deserialize, Deserializer, Serialize};
use serde_json::Value;

#[derive(Debug, Deserialize, Serialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct CallTracerConfig {
    only_top_call: bool,
    with_log: bool,
}

#[derive(Debug, Deserialize, Serialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct PrestateTracerConfig {
    diff_mode: bool,
}

#[derive(Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct OpcodeConfig {
    enable_memory: bool,
    disable_stack: bool,
    disable_storage: bool,
    enable_return_data: bool,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub enum Tracer {
    CallTracer(Option<CallTracerConfig>),
    PrestateTracer(Option<PrestateTracerConfig>),
    FourByteTracer,
    Opcode(Option<OpcodeConfig>),
}

impl Tracer {
    pub fn default() -> Self {
        Tracer::Opcode(Some(OpcodeConfig {
            enable_memory: true,
            disable_stack: false,
            disable_storage: true,
            enable_return_data: false,
        }))
    }
}

impl<'de> Deserialize<'de> for Tracer {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        let value: Value = Deserialize::deserialize(deserializer)?;

        match value["tracer"].as_str() {
            None => {
                let config = value["tracerConfig"].clone();
                Ok(Tracer::Opcode(serde_json::from_value(config).map_err(Error::custom)?))
            }

            Some(tracer_type) => match tracer_type {
                "4byteTracer" => Ok(Tracer::FourByteTracer),
                "callTracer" => Ok(Tracer::CallTracer(serde_json::from_value(value["tracerConfig"].clone()).map_err(Error::custom)?)),
                "prestateTracer" => Ok(Tracer::PrestateTracer(serde_json::from_value(value["tracerConfig"].clone()).map_err(Error::custom)?)),
                _ => Err(Error::custom("Unknown tracer"))
            },
        }
    }
}
