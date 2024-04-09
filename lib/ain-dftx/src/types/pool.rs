use ain_macros::ConsensusEncoding;
use bitcoin::{io, ScriptBuf};

use super::{
    balance::{ScriptBalances, TokenBalanceUInt32, TokenBalanceVarInt},
    common::{CompactVec, Maybe},
};
use crate::common::VarInt;

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct MaxPrice {
    integer: i64,
    fraction: i64,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct PoolSwap {
    pub from_script: ScriptBuf,
    pub from_token_id: VarInt,
    pub from_amount: i64,
    pub to_script: ScriptBuf,
    pub to_token_id: VarInt,
    pub max_price: MaxPrice,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct PoolId {
    pub id: VarInt,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct CompositeSwap {
    pub pool_swap: PoolSwap,
    pub pools: CompactVec<PoolId>,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct PoolAddLiquidity {
    pub from: CompactVec<ScriptBalances>,
    pub share_address: ScriptBuf,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct PoolRemoveLiquidity {
    pub script: ScriptBuf,
    pub amount: TokenBalanceVarInt,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct PoolCreatePair {
    pub token_a: VarInt,
    pub token_b: VarInt,
    pub commission: i64,
    pub owner_address: ScriptBuf,
    pub status: u8,
    pub pair_symbol: String,
    pub custom_rewards: Maybe<CompactVec<TokenBalanceUInt32>>,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct PoolUpdatePair {
    pub pool_id: u32,
    pub status: u8,
    pub commission: i64,
    pub owner_address: ScriptBuf,
    pub custom_rewards: Maybe<CompactVec<TokenBalanceUInt32>>,
}
