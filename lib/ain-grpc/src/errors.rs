use ain_evm::EVMError;
use jsonrpsee::core::Error;

pub enum RPCError {
    EvmCall(EVMError),
    InsufficientFunds,
    ValueOverflow,
    InvalidGasPrice,
    InvalidTransactionType,
    InvalidDataInput,
    TxExecutionFailed,
    GasCapTooLow(u64),
}

impl From<RPCError> for Error {
    fn from(e: RPCError) -> Self {
        match e {
            RPCError::EvmCall(e) => Error::Custom(format!("error calling EVM : {e:?}")),
            RPCError::InsufficientFunds => to_custom_err("insufficient funds for transfer"),
            RPCError::ValueOverflow => to_custom_err("value overflow"),
            RPCError::InvalidGasPrice => {
                to_custom_err("both gasPrice and (maxFeePerGas or maxPriorityFeePerGas) specified")
            }
            RPCError::InvalidTransactionType => to_custom_err("invalid transaction type specified"),
            RPCError::InvalidDataInput => {
                to_custom_err("data and input fields are mutually exclusive")
            }
            RPCError::TxExecutionFailed => to_custom_err("transaction execution failed"),
            RPCError::GasCapTooLow(cap) => {
                Error::Custom(format!("gas required exceeds allowance {:#?}", cap))
            }
        }
    }
}

pub fn to_custom_err<T: ToString>(e: T) -> Error {
    Error::Custom(e.to_string())
}
