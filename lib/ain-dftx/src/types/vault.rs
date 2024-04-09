use ain_macros::ConsensusEncoding;
use bitcoin::{io, ScriptBuf, Txid};

use super::balance::TokenBalanceVarInt;

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct CreateVault {
    pub owner_address: ScriptBuf,
    pub scheme_id: String,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct UpdateVault {
    pub vault_id: Txid,
    pub owner_address: ScriptBuf,
    pub scheme_id: String,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct DepositToVault {
    pub vault_id: Txid,
    pub from: ScriptBuf,
    pub token_amount: TokenBalanceVarInt,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct WithdrawFromVault {
    pub vault_id: Txid,
    pub to: ScriptBuf,
    pub token_amount: TokenBalanceVarInt,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct CloseVault {
    pub vault_id: Txid,
    pub to: ScriptBuf,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct PlaceAuctionBid {
    pub vault_id: Txid,
    pub index: u32,
    pub from: ScriptBuf,
    pub token_amount: TokenBalanceVarInt,
}
