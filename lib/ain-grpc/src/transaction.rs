use ain_evm::transaction::{SignedTx, TransactionError};
use ethereum::{BlockAny, TransactionV2};
use primitive_types::{H256, U256};

use crate::{
    codegen::types::EthTransactionInfo,
    utils::{format_address, format_h256, format_u256},
};

impl From<SignedTx> for EthTransactionInfo {
    fn from(signed_tx: SignedTx) -> Self {
        let input = if signed_tx.data().is_empty() {
            String::from("0x")
        } else {
            format!("0x{}", hex::encode(signed_tx.data()))
        };

        EthTransactionInfo {
            hash: format_h256(signed_tx.transaction.hash()),
            from: format_address(signed_tx.sender),
            to: signed_tx.to().map(format_address),
            gas: format_u256(signed_tx.gas_limit()),
            gas_price: format_u256(signed_tx.gas_price()),
            value: format_u256(signed_tx.value()),
            input,
            nonce: format_u256(signed_tx.nonce()),
            v: format!("0x{:x}", signed_tx.v()),
            r: format_h256(signed_tx.r()),
            s: format_h256(signed_tx.s()),
            block_hash: None,
            block_number: None,
            transaction_index: None,
        }
    }
}

impl TryFrom<TransactionV2> for EthTransactionInfo {
    type Error = TransactionError;

    fn try_from(tx: TransactionV2) -> Result<Self, Self::Error> {
        let signed_tx: SignedTx = tx.try_into()?;
        Ok(signed_tx.into())
    }
}

impl TryFrom<&str> for EthTransactionInfo {
    type Error = TransactionError;

    fn try_from(raw_tx: &str) -> Result<Self, Self::Error> {
        let signed_tx: SignedTx = raw_tx.try_into()?;
        Ok(signed_tx.into())
    }
}

impl EthTransactionInfo {
    pub fn try_from_tx_block_and_index(
        tx: &TransactionV2,
        block: &BlockAny,
        index: usize,
    ) -> Result<Self, TransactionError> {
        let signed_tx: SignedTx = tx.clone().try_into()?;

        Ok(EthTransactionInfo {
            block_hash: Some(format_h256(block.header.hash())),
            block_number: Some(format_u256(block.header.number)),
            transaction_index: Some(format_u256(U256::from(index))),
            ..EthTransactionInfo::from(signed_tx)
        })
    }

    pub fn into_pending_transaction_info(self) -> Self {
        Self {
            block_hash: Some(format_h256(H256::zero())),
            block_number: Some(String::from("null")),
            transaction_index: Some(String::from("0x0")),
            ..self
        }
    }
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct TraceLogs {
    pub pc: u64,
    pub op: String,
    pub gas: u64,
    pub gas_cost: u64,
    pub stack: Vec<String>,
    pub memory: Vec<String>
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct TraceTransactionResult {
    pub gas: U256,
    pub failed: bool,
    pub return_value: String,
    pub struct_logs: Vec<TraceLogs>
}
