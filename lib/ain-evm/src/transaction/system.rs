use anyhow::format_err;
use ethereum_types::H160;

use super::SignedTx;
use crate::Result;
use ain_contracts::dst20_address_from_token_id;
use ain_cpp_imports::{SystemTxData, SystemTxType};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DeployContractData {
    pub name: String,
    pub symbol: String,
    pub address: H160,
    pub token_id: u64,
}
pub type UpdateContractNameData = DeployContractData;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DST20Data {
    pub signed_tx: Box<SignedTx>,
    pub contract_address: H160,
    pub direction: TransferDirection,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TransferDomainData {
    pub signed_tx: Box<SignedTx>,
    pub direction: TransferDirection,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SystemTx {
    DeployContract(DeployContractData),
    DST20Bridge(DST20Data),
    TransferDomain(TransferDomainData),
    UpdateContractName(UpdateContractNameData),
}

impl SystemTx {
    pub fn sender(&self) -> Option<H160> {
        match self {
            SystemTx::TransferDomain(data) => Some(data.signed_tx.sender),
            SystemTx::DST20Bridge(data) => Some(data.signed_tx.sender),
            SystemTx::DeployContract(_) => None,
            SystemTx::UpdateContractName(_) => None,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TransferDirection {
    EvmIn,
    EvmOut,
}

impl From<bool> for TransferDirection {
    fn from(direction: bool) -> TransferDirection {
        if direction {
            TransferDirection::EvmIn
        } else {
            TransferDirection::EvmOut
        }
    }
}

use std::fmt;

impl fmt::Display for TransferDirection {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            TransferDirection::EvmIn => write!(f, "EVM In"),
            TransferDirection::EvmOut => write!(f, "EVM Out"),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ExecuteTx {
    SignedTx(Box<SignedTx>),
    SystemTx(SystemTx),
}

impl From<SignedTx> for ExecuteTx {
    fn from(tx: SignedTx) -> Self {
        Self::SignedTx(Box::new(tx))
    }
}

impl ExecuteTx {
    pub fn from_tx_data(tx_data: SystemTxData, tx: SignedTx) -> Result<Self> {
        match tx_data.tx_type {
            SystemTxType::EVMTx => Ok(Self::SignedTx(Box::new(tx))),
            SystemTxType::TransferDomainIn => Ok(Self::SystemTx(SystemTx::TransferDomain(
                TransferDomainData {
                    signed_tx: Box::new(tx),
                    direction: TransferDirection::EvmIn,
                },
            ))),
            SystemTxType::TransferDomainOut => Ok(ExecuteTx::SystemTx(SystemTx::TransferDomain(
                TransferDomainData {
                    signed_tx: Box::new(tx),
                    direction: TransferDirection::EvmOut,
                },
            ))),
            SystemTxType::DST20BridgeIn => {
                let contract_address = dst20_address_from_token_id(tx_data.token.id)?;
                Ok(ExecuteTx::SystemTx(SystemTx::DST20Bridge(DST20Data {
                    signed_tx: Box::new(tx),
                    contract_address,
                    direction: TransferDirection::EvmIn,
                })))
            }
            SystemTxType::DST20BridgeOut => {
                let contract_address = dst20_address_from_token_id(tx_data.token.id)?;
                Ok(ExecuteTx::SystemTx(SystemTx::DST20Bridge(DST20Data {
                    signed_tx: Box::new(tx),
                    contract_address,
                    direction: TransferDirection::EvmOut,
                })))
            }
            SystemTxType::DeployContract => {
                let address = dst20_address_from_token_id(tx_data.token.id)?;
                Ok(ExecuteTx::SystemTx(SystemTx::DeployContract(
                    UpdateContractNameData {
                        name: tx_data.token.name,
                        symbol: tx_data.token.symbol,
                        address,
                        token_id: tx_data.token.id,
                    },
                )))
            }
            SystemTxType::UpdateContractName => {
                let address = dst20_address_from_token_id(tx_data.token.id)?;
                Ok(ExecuteTx::SystemTx(SystemTx::UpdateContractName(
                    UpdateContractNameData {
                        name: tx_data.token.name,
                        symbol: tx_data.token.symbol,
                        address,
                        token_id: tx_data.token.id,
                    },
                )))
            }
            _ => {
                Err(format_err!("Cannot get execute tx from tx data, system tx type error.").into())
            }
        }
    }
}
