use ain_evm::EVMError;
use ethereum_types::U256;
use evm::{ExitError, ExitReason};
use jsonrpsee::{
    core::{Error, RpcResult},
    types::error::{CallError, ErrorObject, INTERNAL_ERROR_CODE},
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
                return Err(internal_err("out of gas"));
            }
            Err(internal_err_with_data(format!("evm error: {err:?}"), &[]))
        }
        ExitReason::Revert(_) => {
            const LEN_START: usize = 36;
            const MESSAGE_START: usize = 68;

            let mut message = "execution reverted:".to_string();
            // A minimum size of error function selector (4) + offset (32) + string length (32)
            // should contain a utf-8 encoded revert reason.
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
            Err(internal_err_with_data(message, data))
        }
        ExitReason::Fatal(err) => Err(internal_err_with_data(format!("evm error: {err:?}"), &[])),
    }
}

fn err<T: ToString>(code: i32, message: T, data: Option<&[u8]>) -> Error {
    Error::Call(CallError::Custom(ErrorObject::owned(
        code,
        message.to_string(),
        data.map(|bytes| {
            jsonrpsee::core::to_json_raw_value(&format!("0x{}", hex::encode(bytes)))
                .expect("fail to serialize data")
        }),
    )))
}

fn internal_err<T: ToString>(message: T) -> Error {
    err(INTERNAL_ERROR_CODE, message, None)
}

fn internal_err_with_data<T: ToString>(message: T, data: &[u8]) -> Error {
    err(INTERNAL_ERROR_CODE, message, Some(data))
}
