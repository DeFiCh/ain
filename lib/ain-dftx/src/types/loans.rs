use ain_macros::ConsensusEncoding;
use bitcoin::{io, ScriptBuf, Txid, VarInt};

use super::{balance::TokenBalanceUInt32, common::CompactVec, price::CurrencyPair};

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct SetLoanScheme {
    pub ratio: u32,
    pub rate: i64,
    pub identifier: String,
    pub update: i64,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct DestroyLoanScheme {
    pub identifier: String,
    pub height: i64,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct SetDefaultLoanScheme {
    pub identifier: String,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct SetCollateralToken {
    pub token: VarInt,
    pub factor: i64,
    pub currency_pair: CurrencyPair,
    pub activate_after_block: u32,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct SetLoanToken {
    pub symbol: String,
    pub name: String,
    pub currency_pair: CurrencyPair,
    pub mintable: bool,
    pub interest: i64,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct UpdateLoanToken {
    pub symbol: String,
    pub name: String,
    pub currency_pair: CurrencyPair,
    pub mintable: bool,
    pub interest: i64,
    pub token_tx: Txid,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct TakeLoan {
    pub vault_id: Txid,
    pub to: ScriptBuf,
    pub token_amounts: CompactVec<TokenBalanceUInt32>,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct PaybackLoan {
    pub vault_id: Txid,
    pub from: ScriptBuf,
    pub token_amounts: CompactVec<TokenBalanceUInt32>,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct TokenPayback {
    pub d_token: VarInt,
    pub amounts: CompactVec<TokenBalanceUInt32>,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct PaybackLoanV2 {
    pub vault_id: Txid,
    pub from: ScriptBuf,
    pub loans: CompactVec<TokenPayback>,
}
