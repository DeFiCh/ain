use std::fmt::Write;

use ain_evm::{services::SERVICES, storage::block_store::DumpArg, Result};
use ain_macros::ffi_fallible;
use anyhow::format_err;
use ethereum::Account;
use rlp::{Decodable, Rlp};

use crate::{ffi, prelude::*};

#[ffi_fallible]
pub fn debug_dump_db(arg: String, start: String, limit: String) -> Result<String> {
    if !ain_cpp_imports::is_eth_debug_rpc_enabled() {
        return Err("debug_* RPCs have not been enabled".into());
    }

    let arg = if arg.is_empty() {
        DumpArg::All
    } else {
        DumpArg::try_from(arg)?
    };

    let default_limit = 100usize;
    let limit = if limit.is_empty() {
        default_limit
    } else {
        limit.as_str().parse().map_err(|_| "Invalid limit")?
    };

    let start = if start.is_empty() {
        None
    } else {
        Some(start.as_str())
    };

    SERVICES.evm.storage.dump_db(arg, start, limit)
}

#[ffi_fallible]
pub fn debug_log_account_states() -> Result<String> {
    if !ain_cpp_imports::is_eth_debug_rpc_enabled() {
        return Err("debug_* RPCs have not been enabled".into());
    }

    let backend = SERVICES.evm.core.get_latest_block_backend()?;
    let ro_handle = backend.ro_handle();

    let mut out = String::new();
    let response_max_size = usize::try_from(ain_cpp_imports::get_max_response_byte_size())
        .map_err(|_| format_err!("failed to convert response size limit to usize"))?;

    for el in ro_handle.iter() {
        if out.len() > response_max_size {
            return Err(format_err!("exceed response max size limit").into());
        }

        match el {
            Ok((_, v)) => {
                if let Ok(account) = Account::decode(&Rlp::new(&v)) {
                    writeln!(&mut out, "Account {:?}:", account)
                        .map_err(|_| format_err!("failed to write to stream"))?;
                } else {
                    writeln!(&mut out, "Error decoding account {:?}", v)
                        .map_err(|_| format_err!("failed to write to stream"))?;
                }
            }
            Err(e) => {
                writeln!(&mut out, "Error on iter element {e}")
                    .map_err(|_| format_err!("failed to write to stream"))?;
            }
        };
    }

    Ok(out)
}
