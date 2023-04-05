use ethereum::AccessList;
use evm::Config;
use primitive_types::{H160, U256};

use crate::{executor::TxResponse, transaction::SignedTx};

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
    ) -> TxResponse;

    fn exec(&mut self, tx: &SignedTx) -> TxResponse;
}
