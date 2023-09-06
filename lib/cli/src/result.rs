use ain_grpc::{block::RpcBlock, codegen::types::EthTransactionInfo};
use ethereum_types::{H256, U256};
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
#[serde(untagged)]
pub enum RpcResult {
    String(String),
    VecString(Vec<String>),
    U256(U256),
    H256(H256),
    Bool(bool),
    EthTransactionInfo(EthTransactionInfo),
    Usize(usize),
    Block(Box<RpcBlock>),
    Option(Option<Box<RpcResult>>),
}

impl From<String> for RpcResult {
    fn from(value: String) -> Self {
        RpcResult::String(value)
    }
}

impl From<Vec<String>> for RpcResult {
    fn from(value: Vec<String>) -> Self {
        RpcResult::VecString(value)
    }
}

impl From<U256> for RpcResult {
    fn from(value: U256) -> Self {
        RpcResult::U256(value)
    }
}

impl From<H256> for RpcResult {
    fn from(value: H256) -> Self {
        RpcResult::H256(value)
    }
}

impl From<bool> for RpcResult {
    fn from(value: bool) -> Self {
        RpcResult::Bool(value)
    }
}

impl From<EthTransactionInfo> for RpcResult {
    fn from(value: EthTransactionInfo) -> Self {
        RpcResult::EthTransactionInfo(value)
    }
}

impl From<usize> for RpcResult {
    fn from(value: usize) -> Self {
        RpcResult::Usize(value)
    }
}

impl From<RpcBlock> for RpcResult {
    fn from(value: RpcBlock) -> Self {
        RpcResult::Block(Box::new(value))
    }
}

impl<T: Into<RpcResult> + 'static> From<Option<T>> for RpcResult {
    fn from(value: Option<T>) -> Self {
        RpcResult::Option(value.map(|v| Box::new(v.into())))
    }
}

use std::fmt::{self, Display, Formatter};

impl Display for RpcResult {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        match self {
            RpcResult::String(value) => write!(f, "{value}"),
            RpcResult::VecString(value) => write!(f, "{value:?}"),
            RpcResult::U256(value) => write!(f, "{value:#x}"),
            RpcResult::H256(value) => write!(f, "{value:#x}"),
            RpcResult::Bool(value) => write!(f, "{value}"),
            RpcResult::EthTransactionInfo(value) => write!(f, "{value:?}"),
            RpcResult::Usize(value) => write!(f, "{value}"),
            RpcResult::Block(value) => write!(f, "{value:?}"),
            RpcResult::Option(value) => match value {
                Some(inner_value) => write!(f, "Some({inner_value})"),
                None => write!(f, "None"),
            },
        }
    }
}
