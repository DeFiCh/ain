use super::common::CompactVec;
use ain_macros::ConsensusEncoding;
use bitcoin::io;
use serde::Serialize;

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct CurrencyPair {
    pub token: String,
    pub currency: String,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone, Serialize)]
pub struct TokenAmount {
    pub currency: String,
    pub amount: i64,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq, Clone)]
pub struct TokenPrice {
    pub token: String,
    pub prices: CompactVec<TokenAmount>,
}
