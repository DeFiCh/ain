use ain_macros::ConsensusEncoding;
use bitcoin::{hash_types::Txid, io, ScriptBuf};

use super::{
    common::CompactVec,
    price::{CurrencyPair, TokenPrice},
    Weightage,
};

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct SetOracleData {
    pub oracle_id: Txid,
    pub timestamp: i64,
    pub token_prices: CompactVec<TokenPrice>,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct RemoveOracle {
    pub oracle_id: Txid,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct AppointOracle {
    pub script: ScriptBuf,
    pub weightage: Weightage,
    pub price_feeds: CompactVec<CurrencyPair>,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct UpdateOracle {
    pub oracle_id: Txid,
    pub script: ScriptBuf,
    pub weightage: Weightage,
    pub price_feeds: CompactVec<CurrencyPair>,
}
