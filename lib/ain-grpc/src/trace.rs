use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
#[serde(rename_all = "camelCase")]
pub struct TraceParams {
    pub disable_storage: Option<bool>,
    pub disable_memory: Option<bool>,
    pub disable_stack: Option<bool>,
    /// Built-in tracer (callTracer) or Javascript expression (blockscout tracer).
    pub tracer: Option<String>,
    pub timeout: Option<String>,
}

#[derive(Clone, Copy, Eq, PartialEq, Debug)]
pub enum TracerInput {
	None,
	Blockscout,
	CallTracer,
}

/// DebugRuntimeApi V2 result. Trace response is stored in client and runtime api call response is
/// empty.
#[derive(Debug)]
pub enum Response {
	Single,
	Block,
}

fn handle_params(params: Option<TraceParams>) -> RpcResult<(TracerInput, single::TraceType)> {
    // Set trace input and type
    match params {
        Some(TraceParams {
            tracer: Some(tracer),
            ..
        }) => {
            const BLOCKSCOUT_JS_CODE_HASH: [u8; 16] =
                hex_literal::hex!("94d9f08796f91eb13a2e82a6066882f7");
            const BLOCKSCOUT_JS_CODE_HASH_V2: [u8; 16] =
                hex_literal::hex!("89db13694675692951673a1e6e18ff02");
            let hash = sp_io::hashing::twox_128(&tracer.as_bytes());
            let tracer =
                if hash == BLOCKSCOUT_JS_CODE_HASH || hash == BLOCKSCOUT_JS_CODE_HASH_V2 {
                    Some(TracerInput::Blockscout)
                } else if tracer == "callTracer" {
                    Some(TracerInput::CallTracer)
                } else {
                    None
                };
            if let Some(tracer) = tracer {
                Ok((tracer, single::TraceType::CallList))
            } else {
                return Err(internal_err(format!(
                    "javascript based tracing is not available (hash :{:?})",
                    hash
                )));
            }
        }
        Some(params) => Ok((
            TracerInput::None,
            single::TraceType::Raw {
                disable_storage: params.disable_storage.unwrap_or(false),
                disable_memory: params.disable_memory.unwrap_or(false),
                disable_stack: params.disable_stack.unwrap_or(false),
            },
        )),
        _ => Ok((
            TracerInput::None,
            single::TraceType::Raw {
                disable_storage: false,
                disable_memory: false,
                disable_stack: false,
            },
        )),
    }
}
