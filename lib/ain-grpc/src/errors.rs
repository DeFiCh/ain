use ain_evm::EVMError;
use jsonrpsee::core::Error;

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
            RPCError::BlockNotFound => to_custom_err("block not found"),
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
