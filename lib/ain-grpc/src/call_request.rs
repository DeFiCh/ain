use ain_evm::bytes::Bytes;
use ethereum::AccessListItem;
use ethereum_types::{H160, U256};
use jsonrpsee::core::Error;
use serde::Deserialize;

use crate::errors::RPCError;

/// Call request
#[derive(Clone, Debug, Default, Eq, PartialEq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
#[serde(rename_all = "camelCase")]
pub struct CallRequest {
    /// From
    pub from: Option<H160>,
    /// To
    pub to: Option<H160>,
    /// Gas Price
    pub gas_price: Option<U256>,
    /// EIP-1559 Max base fee the caller is willing to pay
    pub max_fee_per_gas: Option<U256>,
    /// EIP-1559 Priority fee the caller is paying to the block author
    pub max_priority_fee_per_gas: Option<U256>,
    /// Gas
    pub gas: Option<U256>,
    /// Value
    pub value: Option<U256>,
    /// Data
    pub data: Option<Bytes>,
    /// Input
    pub input: Option<Bytes>,
    /// Nonce
    pub nonce: Option<U256>,
    /// AccessList
    pub access_list: Option<Vec<AccessListItem>>,
    /// EIP-2718 type
    #[serde(rename = "type")]
    pub transaction_type: Option<U256>,
}

impl CallRequest {
    pub fn clone_with_guessed_tx_type(&self) -> Result<Self, Error> {
        let mut copy = self.clone();

        if self.transaction_type.is_none() {
            if self.gas_price.is_some()
                && (self.max_fee_per_gas.is_some() || self.max_priority_fee_per_gas.is_some())
            {
                return Err(RPCError::InvalidGasPrice.into());
            }

            if self.max_fee_per_gas.is_some() && self.max_priority_fee_per_gas.is_some() {
                copy.transaction_type = Some(U256::from(2));
            } else if self.access_list.is_some() {
                copy.transaction_type = Some(U256::one());
            } else {
                copy.transaction_type = Some(U256::zero());
            }
        }

        Ok(copy)
    }
    pub fn get_effective_gas_price(&self, block_base_fee: U256) -> Result<U256, Error> {
        if self.gas_price.is_some()
            && (self.max_fee_per_gas.is_some() || self.max_priority_fee_per_gas.is_some())
        {
            return Err(RPCError::InvalidGasPrice.into());
        }

        match self.transaction_type {
            // Legacy || EIP2930
            Some(tx_type) if tx_type == U256::zero() || tx_type == U256::one() => {
                match self.gas_price {
                    Some(gas_price) => {
                        if gas_price == U256::zero() {
                            Ok(block_base_fee)
                        } else {
                            Ok(gas_price)
                        }
                    }
                    None => Ok(block_base_fee),
                }
            }
            // EIP1559
            Some(tx_type) if tx_type == U256::from(2) => match self.max_fee_per_gas {
                Some(max_fee_per_gas) => {
                    if max_fee_per_gas == U256::zero() {
                        Ok(block_base_fee)
                    } else {
                        Ok(max_fee_per_gas)
                    }
                }
                None => Ok(block_base_fee),
            },
            None => Err(RPCError::InvalidTransactionType.into()),
            _ => Err(RPCError::InvalidTransactionType.into()),
        }
    }

    // https://github.com/ethereum/go-ethereum/blob/281e8cd5abaac86ed3f37f98250ff147b3c9fe62/internal/ethapi/transaction_args.go#L67
    // We accept "data" and "input" for backwards-compatibility reasons.
    //  "input" is the newer name and should be preferred by clients.
    // 	Issue detail: https://github.com/ethereum/go-ethereum/issues/15628
    pub fn get_data(&self) -> Result<Bytes, Error> {
        if self.data.is_some() && self.input.is_some() {
            return Err(RPCError::InvalidDataInput.into());
        }

        if let Some(data) = self.data.clone() {
            Ok(data)
        } else if let Some(data) = self.input.clone() {
            Ok(data)
        } else {
            Ok(Default::default())
        }
    }
}
