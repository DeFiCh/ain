use ain_evm::transaction::{SignedTx, TransactionError};
use ethereum::{AccessListItem, BlockAny, EnvelopedEncodable, TransactionV2};
use ethereum_types::{H256, U256};

use crate::{
    codegen::types::{EthAccessList, EthTransactionInfo},
    utils::{format_address, format_h256, format_u256},
};

impl From<SignedTx> for EthTransactionInfo {
    fn from(signed_tx: SignedTx) -> Self {
        let input = if signed_tx.data().is_empty() {
            String::from("0x")
        } else {
            format!("0x{}", hex::encode(signed_tx.data()))
        };

        let access_list: Vec<EthAccessList> = match &signed_tx.transaction {
            TransactionV2::Legacy(_) => Vec::new(),
            TransactionV2::EIP2930(tx) => tx
                .access_list
                .clone()
                .into_iter()
                .map(std::convert::Into::into)
                .collect(),
            TransactionV2::EIP1559(tx) => tx
                .access_list
                .clone()
                .into_iter()
                .map(std::convert::Into::into)
                .collect(),
        };

        EthTransactionInfo {
            hash: format_h256(signed_tx.hash()),
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
            field_type: format_u256(U256::from(
                signed_tx.transaction.type_id().unwrap_or_default(),
            )),
            max_fee_per_gas: signed_tx
                .max_fee_per_gas()
                .map(format_u256)
                .unwrap_or_default(),
            max_priority_fee_per_gas: signed_tx
                .max_priority_fee_per_gas()
                .map(format_u256)
                .unwrap_or_default(),
            access_list,
            chain_id: format!("{:#x}", signed_tx.chain_id()),
        }
    }
}

impl From<AccessListItem> for EthAccessList {
    fn from(access_list: AccessListItem) -> Self {
        Self {
            address: format_address(access_list.address),
            storage_keys: access_list
                .storage_keys
                .into_iter()
                .map(format_h256)
                .collect(),
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
            gas_price: format_u256(
                signed_tx
                    .effective_gas_price(block.header.base_fee)
                    .map_err(|_| TransactionError::ConstructionError)?,
            ),
            ..EthTransactionInfo::from(signed_tx)
        })
    }

    #[must_use]
    pub fn into_pending_transaction_info(self) -> Self {
        Self {
            block_hash: Some(format_h256(H256::zero())),
            block_number: Some(String::from("null")),
            transaction_index: Some(String::from("0x0")),
            ..self
        }
    }
}
