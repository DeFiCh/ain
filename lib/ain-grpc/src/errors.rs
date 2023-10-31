use ain_evm::EVMError;
use ethereum_types::U256;
use evm::{ExitError, ExitReason};
use jsonrpsee::{
    core::{Error, RpcResult},
    types::error::{CallError, ErrorObject},
};

pub enum RPCError {
    AccountError,
    BlockNotFound,
    Error(Box<dyn std::error::Error>),
    EvmError(EVMError),
    FromBlockGreaterThanToBlock,
    GasCapTooLow(u64),
    InsufficientFunds,
    InvalidBlockInput,
    InvalidDataInput,
    InvalidLogFilter,
    InvalidGasPrice,
    InvalidTransactionMessage,
    InvalidTransactionType,
    NonceCacheError,
    StateRootNotFound,
    TxExecutionFailed,
    ValueOverflow,
}

impl From<RPCError> for Error {
    fn from(e: RPCError) -> Self {
        match e {
            RPCError::AccountError => to_custom_err("error getting account"),
            RPCError::BlockNotFound => to_custom_err("header not found"),
            RPCError::Error(e) => Error::Custom(format!("{e:?}")),
            RPCError::EvmError(e) => Error::Custom(format!("error calling EVM : {e:?}")),
            RPCError::FromBlockGreaterThanToBlock => {
                to_custom_err("fromBlock is greater than toBlock")
            }
            RPCError::GasCapTooLow(cap) => {
                Error::Custom(format!("gas required exceeds allowance {:#?}", cap))
            }
            RPCError::InsufficientFunds => to_custom_err("insufficient funds for transfer"),
            RPCError::InvalidBlockInput => {
                to_custom_err("both blockHash and fromBlock/toBlock specified")
            }
            RPCError::InvalidDataInput => {
                to_custom_err("data and input fields are mutually exclusive")
            }
            RPCError::InvalidGasPrice => {
                to_custom_err("both gasPrice and (maxFeePerGas or maxPriorityFeePerGas) specified")
            }
            RPCError::InvalidLogFilter => to_custom_err("invalid log filter"),
            RPCError::InvalidTransactionMessage => {
                to_custom_err("invalid transaction message parameters")
            }
            RPCError::InvalidTransactionType => to_custom_err("invalid transaction type specified"),
            RPCError::NonceCacheError => to_custom_err("could not cache account nonce"),
            RPCError::StateRootNotFound => to_custom_err("state root not found"),
            RPCError::TxExecutionFailed => to_custom_err("transaction execution failed"),
            RPCError::ValueOverflow => to_custom_err("value overflow"),
        }
    }
}

pub fn to_custom_err<T: ToString>(e: T) -> Error {
    Error::Custom(e.to_string())
}

pub fn error_on_execution_failure(reason: &ExitReason, data: &[u8]) -> RpcResult<()> {
    match reason {
        ExitReason::Succeed(_) => Ok(()),
        ExitReason::Error(err) => {
            if *err == ExitError::OutOfGas {
                return Err(Error::Custom(format!("out of gas")))
            }
            Err(Error::Custom(format!("evm error: {err:?}")))
        }
        ExitReason::Revert(_) => {
            const LEN_START: usize = 36;
            const MESSAGE_START: usize = 68;

            let mut message = "execution reverted:".to_string();
            // A minimum size of error function selector (4) + offset (32) + string length (32)
            // should contain a utf-8 encoded revert reason.
            //
            // 0x08c379a0                                                         // Function selector for Error(string)
            // 0x0000000000000000000000000000000000000000000000000000000000000020 // Data offset
            // 0x000000000000000000000000000000000000000000000000000000000000001a // String length
            // 0x4e6f7420656e6f7567682045746865722070726f76696465642e000000000000 // String data
            //
            // https://docs.soliditylang.org/en/latest/control-structures.html#revert
            if data.len() > MESSAGE_START {
                let message_len = U256::from(&data[LEN_START..MESSAGE_START]).as_usize();
                let message_end = MESSAGE_START.saturating_add(message_len);

                if data.len() >= message_end {
                    let body: &[u8] = &data[MESSAGE_START..message_end];
                    if let Ok(reason) = std::str::from_utf8(body) {
                        message = format!("{message} {reason}");
                    }
                }
            }
            Err(Error::Call(CallError::Custom(ErrorObject::owned(
                3,
                message.to_string(),
                Some(data).map(|bytes| {
                    jsonrpsee::core::to_json_raw_value(&format!("0x{}", hex::encode(bytes)))
                        .expect("fail to serialize data")
                }),
            ))))
        }
        ExitReason::Fatal(err) => Err(Error::Custom(format!("evm error: {err:?}"))),
    }
}
