use ethereum_types::H160;

use super::SignedTx;

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
