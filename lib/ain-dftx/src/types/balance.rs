use ain_macros::ConsensusEncoding;
use bitcoin::{io, ScriptBuf, VarInt};
use serde::Serialize;

use super::common::CompactVec;

// CBalances
#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct TokenBalanceUInt32 {
    pub token: u32,
    pub amount: i64,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct ScriptBalances {
    pub script: ScriptBuf,
    pub balances: CompactVec<TokenBalanceUInt32>,
}

// CTokenAmount
#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct TokenBalanceVarInt {
    pub token: VarInt,
    pub amount: i64,
}
