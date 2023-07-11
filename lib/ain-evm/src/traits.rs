use crate::{executor::TxResponse, transaction::SignedTx};
use ethereum::{AccessList, ReceiptV3};
use evm::Config;
use primitive_types::{H160, U256};
use crate::Result;

#[derive(Debug)]
pub struct ExecutorContext<'a> {
    pub caller: H160,
    pub to: Option<H160>,
    pub value: U256,
    pub data: &'a [u8],
    pub gas_limit: u64,
    pub access_list: AccessList,
}

pub trait Executor {
    const CONFIG: Config = Config::shanghai();

    fn call(&mut self, ctx: ExecutorContext) -> TxResponse;

    fn exec(&mut self, tx: &SignedTx) -> (TxResponse, ReceiptV3);
}

pub trait BridgeBackend {
    fn add_balance(&mut self, address: H160, amount: U256) -> Result<()>;

    fn sub_balance(&mut self, address: H160, amount: U256) -> Result<()>;
}
