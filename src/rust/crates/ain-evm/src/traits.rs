use ethereum::AccessList;
use evm::{backend::MemoryAccount, Config, ExitReason};
use primitive_types::{H160, U256};

use crate::transaction::SignedTx;

pub trait Executor {
    const CONFIG: Config = Config::london();

    fn call(
        &mut self,
        caller: Option<H160>,
        to: Option<H160>,
        value: U256,
        data: &[u8],
        gas_limit: u64,
        access_list: AccessList,
        apply: bool,
    ) -> (ExitReason, Vec<u8>);

    fn exec(&mut self, tx: &SignedTx) -> (ExitReason, Vec<u8>);
}
