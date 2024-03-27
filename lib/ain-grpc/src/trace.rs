// Copyright 2019-2022 PureStake Inc.
// This file is part of Moonbeam.

// Moonbeam is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Moonbeam is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Moonbeam.  If not, see <http://www.gnu.org/licenses/>.

use ain_evm::trace::types::single;
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

#[derive(Clone, Copy, Eq, PartialEq, Debug)]
pub enum TracerInput {
    None,
    Blockscout,
    CallTracer,
}

/// DebugRuntimeApi V2 result. Trace response is stored in client and runtime api call response is
/// empty.
// #[derive(Debug)]
// pub enum Response {
//     Single,
//     Block,
// }

pub fn handle_trace_params(
    params: Option<TraceParams>,
) -> RpcResult<(TracerInput, single::TraceType)> {
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
                Ok((tracer, single::TraceType::CallList))
            } else {
                Err(RPCError::TracingParamError(hash).into())
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
