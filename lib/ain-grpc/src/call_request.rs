use std::collections::{BTreeMap, HashMap};

use ain_evm::{backend::Overlay, bytes::Bytes};
use ethereum::{AccessListItem, Account};
use ethereum_types::{H160, H256, U256};
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
    pub fn get_effective_gas_price(&self, block_base_fee: U256) -> Result<U256, Error> {
        if self.gas_price.is_some()
            && (self.max_fee_per_gas.is_some() || self.max_priority_fee_per_gas.is_some())
        {
            return Err(RPCError::InvalidGasPrice.into());
        }

        match self.transaction_type {
            // Legacy
            Some(tx_type) if tx_type == U256::zero() => {
                if let Some(gas_price) = self.gas_price {
                    return Ok(gas_price);
                } else {
                    return Ok(block_base_fee);
                }
            }
            // EIP2930
            Some(tx_type) if tx_type == U256::one() => {
                if let Some(gas_price) = self.gas_price {
                    return Ok(gas_price);
                } else {
                    return Ok(block_base_fee);
                }
            }
            // EIP1559
            Some(tx_type) if tx_type == U256::from(2) => {
                if let Some(max_fee_per_gas) = self.max_fee_per_gas {
                    return Ok(max_fee_per_gas);
                } else {
                    return Ok(block_base_fee);
                }
            }
            None => (),
            _ => return Err(RPCError::InvalidTransactionType.into()),
        }

        if let Some(gas_price) = self.gas_price {
            Ok(gas_price)
        } else if let Some(gas_price) = self.max_fee_per_gas {
            Ok(gas_price)
        } else {
            Ok(block_base_fee)
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

// State override
#[derive(Clone, Debug, Default, Eq, PartialEq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
#[serde(rename_all = "camelCase")]
pub struct CallStateOverride {
    /// Fake balance to set for the account before executing the call.
    pub balance: Option<U256>,
    /// Fake nonce to set for the account before executing the call.
    pub nonce: Option<U256>,
    /// Fake EVM bytecode to inject into the account before executing the call.
    pub code: Option<Bytes>,
    /// Fake key-value mapping to override all slots in the account storage before
    /// executing the call.
    pub state: Option<BTreeMap<H256, H256>>,
    /// Fake key-value mapping to override individual slots in the account storage before
    /// executing the call.
    pub state_diff: Option<BTreeMap<H256, H256>>,
}

pub fn override_to_overlay(overide: BTreeMap<H160, CallStateOverride>) -> Overlay {
    let mut overlay = Overlay::default();

    for (address, state_override) in overide {
        let code = state_override.code.map(|b| b.into_vec());
        let mut storage = state_override
            .state
            .unwrap_or_default()
            .into_iter()
            .collect::<HashMap<_, _>>();

        let account = Account {
            balance: state_override.balance.unwrap_or_default(),
            nonce: state_override.nonce.unwrap_or_default(),
            storage_root: H256::zero(),
            code_hash: H256::zero(),
        };

        let reset_storage = storage.is_empty();
        if let Some(diff) = state_override.state_diff {
            for (k, v) in diff {
                storage.insert(k, v);
            }
        }

        overlay.apply(address, account, code, storage, reset_storage);
    }

    overlay
}
