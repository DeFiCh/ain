use ain_macros::ConsensusEncoding;
use bitcoin::{io, ScriptBuf, VarInt};

use super::common::CompactVec;

// CBalances
#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct TokenBalanceUInt32 {
    pub token: u32,
    pub amount: i64,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct ScriptBalances {
    pub script: ScriptBuf,
    pub balances: CompactVec<TokenBalanceUInt32>,
}

// CTokenAmount
#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct TokenBalanceVarInt {
    pub token: VarInt,
    pub amount: i64,
}
