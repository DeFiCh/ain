use ain_macros::ConsensusEncoding;
use bitcoin::{io, ScriptBuf, VarInt};

use super::{
    balance::{ScriptBalances, TokenBalanceUInt32, TokenBalanceVarInt},
    common::CompactVec,
};

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct UtxosToAccount {
    pub to: CompactVec<ScriptBalances>,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct AccountToUtxos {
    pub from: ScriptBuf,
    pub balances: CompactVec<TokenBalanceUInt32>,
    pub minting_outputs_start: VarInt,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct AccountToAccount {
    pub from: ScriptBuf,
    pub to: CompactVec<ScriptBalances>,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct AnyAccountsToAccounts {
    pub from: CompactVec<ScriptBalances>,
    pub to: CompactVec<ScriptBalances>,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct TransferDomainItem {
    pub address: ScriptBuf,
    pub amount: TokenBalanceVarInt,
    pub domain: u8,
    pub data: Vec<u8>,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct TransferDomainPair {
    pub src: TransferDomainItem,
    pub dst: TransferDomainItem,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct TransferDomain {
    pub items: CompactVec<TransferDomainPair>,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct SetFutureSwap {
    pub owner: ScriptBuf,
    pub source: TokenBalanceVarInt,
    pub destination: u32,
    pub withdraw: bool,
}
