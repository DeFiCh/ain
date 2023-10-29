use ain_evm::EVMError;
use jsonrpsee::core::Error;

pub fn to_custom_err<T: ToString>(e: T) -> Error {
    jsonrpsee::core::Error::Custom(e.to_string())
}

pub fn evm_call_err(e: EVMError) -> Error {
    Error::Custom(format!("error calling EVM : {e:?}"))
}

pub fn no_sender_address_err() -> Error {
    Error::Custom(String::from("no from address specified"))
}

pub fn insufficient_funds_for_transfer_err() -> Error {
    Error::Custom(String::from("insufficient funds for transfer"))
}

pub fn value_overflow_err() -> Error {
    Error::Custom(String::from("value overflow"))
}

pub fn invalid_specified_gas_price_err() -> Error {
    Error::Custom(String::from(
        "both gasPrice and (maxFeePerGas or maxPriorityFeePerGas) specified",
    ))
}

pub fn invalid_transaction_type_err() -> Error {
    Error::Custom(String::from("invalid transaction type specified"))
}

pub fn invalid_data_err() -> Error {
    Error::Custom(String::from("data and input fields are mutually exclusive"))
}

pub fn tx_execution_failure_err() -> Error {
    Error::Custom(String::from("transaction execution failed"))
}

pub fn gas_cap_too_low_err(cap: u64) -> Error {
    Error::Custom(format!("gas required exceeds allowance {:#?}", cap))
}
