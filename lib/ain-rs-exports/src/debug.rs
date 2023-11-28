use std::fmt::Write;

use ain_evm::{services::SERVICES, storage::block_store::DumpArg, Result};
use ain_macros::ffi_fallible;
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

    let start = if start.is_empty() { None } else { Some(start.as_str()) };
    SERVICES.evm.storage.dump_db(arg, start, limit)
}

#[ffi_fallible]
pub fn debug_log_account_state() -> Result<String> {
    if !ain_cpp_imports::is_eth_debug_rpc_enabled() {
        return Err("debug_* RPCs have not been enabled".into());
    }

    let backend = SERVICES
        .evm
        .core
        .get_latest_block_backend()
        .expect("Error restoring backend");
    let ro_handle = backend.ro_handle();

    let mut out = String::new();
    ro_handle.iter().for_each(|el| match el {
        Ok((_, v)) => {
            if let Ok(account) = Account::decode(&Rlp::new(&v)) {
                writeln!(&mut out, "Account {:?}:", account);
            } else {
                writeln!(&mut out, "Error decoding account {:?}", v);
            }
        }
        Err(e) => {
            writeln!(&mut out, "Error on iter element {e}");
        }
    });

    Ok(())
}
