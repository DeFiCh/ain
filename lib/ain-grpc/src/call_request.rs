use std::collections::{BTreeMap, HashMap};

use ain_evm::{backend::Overlay, bytes::Bytes, trace::service::AccessListInfo};
use ethereum::{AccessList, AccessListItem, Account};
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

enum TxType {
    Legacy,
    EIP2930,
    EIP1559,
}

impl TryFrom<U256> for TxType {
    type Error = Error;

    fn try_from(v: U256) -> Result<Self, Self::Error> {
        match v {
            v if v == U256::zero() => Ok(Self::Legacy),
            v if v == U256::one() => Ok(Self::EIP2930),
            v if v == U256::from(2) => Ok(Self::EIP1559),
            _ => Err(RPCError::InvalidTransactionType.into()),
        }
    }
}

fn guess_tx_type(req: &CallRequest) -> Result<TxType, Error> {
    if let Some(tx_type) = req.transaction_type {
        return TxType::try_from(tx_type);
    }

    // Validate call request gas fees
    if req.gas_price.is_some()
        && (req.max_fee_per_gas.is_some() || req.max_priority_fee_per_gas.is_some())
    {
        return Err(RPCError::InvalidGasPrice.into());
    }
    if req.max_fee_per_gas.is_some() && req.max_priority_fee_per_gas.is_none() {
        return Err(RPCError::InvalidGasPrice.into());
    }

    if req.max_fee_per_gas.is_some() && req.max_priority_fee_per_gas.is_some() {
        Ok(TxType::EIP1559)
    } else if req.access_list.is_some() {
        Ok(TxType::EIP2930)
    } else {
        Ok(TxType::Legacy)
    }
}

impl CallRequest {
    pub fn get_effective_gas_price(&self) -> Result<Option<U256>, Error> {
        match guess_tx_type(self)? {
            TxType::Legacy | TxType::EIP2930 => {
                if let Some(gas_price) = self.gas_price {
                    if gas_price.is_zero() {
                        return Ok(None);
                    }
                }
                Ok(self.gas_price)
            }
            TxType::EIP1559 => {
                if let Some(max_fee_per_gas) = self.max_fee_per_gas {
                    if max_fee_per_gas.is_zero() {
                        return Ok(None);
                    }
                }
                Ok(self.max_fee_per_gas)
            }
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

pub fn override_to_overlay(r#override: BTreeMap<H160, CallStateOverride>) -> Overlay {
    let mut overlay = Overlay::default();

    for (address, state_override) in r#override {
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

#[derive(Clone, Debug, Default, Eq, PartialEq, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AccessListResult {
    pub access_list: AccessList,
    pub gas_used: U256,
}

impl From<AccessListInfo> for AccessListResult {
    fn from(value: AccessListInfo) -> Self {
        Self {
            access_list: value.access_list,
            gas_used: value.gas_used,
        }
    }
}
