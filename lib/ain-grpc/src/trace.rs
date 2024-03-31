use ain_evm::trace::types::single::{TraceType, TracerInput};
use jsonrpsee::core::RpcResult;
use serde::{Deserialize, Serialize};

use crate::errors::RPCError;

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

pub fn handle_trace_params(params: Option<TraceParams>) -> RpcResult<(TracerInput, TraceType)> {
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
            let hash = sp_io::hashing::twox_128(tracer.as_bytes());
            let tracer = if hash == BLOCKSCOUT_JS_CODE_HASH || hash == BLOCKSCOUT_JS_CODE_HASH_V2 {
                Some(TracerInput::Blockscout)
            } else if tracer == "callTracer" {
                Some(TracerInput::CallTracer)
            } else {
                None
            };
            if let Some(tracer) = tracer {
                Ok((tracer, TraceType::CallList))
            } else {
                Err(RPCError::TracingParamError(hash).into())
            }
        }
        Some(params) => Ok((
            TracerInput::None,
            TraceType::Raw {
                disable_storage: params.disable_storage.unwrap_or(false),
                disable_memory: params.disable_memory.unwrap_or(false),
                disable_stack: params.disable_stack.unwrap_or(false),
            },
        )),
        _ => Ok((
            TracerInput::None,
            TraceType::Raw {
                disable_storage: false,
                disable_memory: false,
                disable_stack: false,
            },
        )),
    }
}
