pub mod debug;
pub mod eth;
pub mod net;
pub mod web3;

pub fn to_jsonrpsee_custom_error<T: ToString>(e: T) -> jsonrpsee::core::Error {
    jsonrpsee::core::Error::Custom(e.to_string())
}
