use ain_macros::ConsensusEncoding;
use bitcoin::{io, ScriptBuf, Txid};

use crate::common::{CompactVec, Maybe, VarInt};

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct ICXCreateOrder {
    pub order_type: u8,
    pub token_id: VarInt,
    pub owner_address: ScriptBuf,
    pub receive_pubkey: Maybe<CompactVec<u8>>,
    pub amount_from: i64,
    pub amount_to_fill: i64,
    pub order_price: i64,
    pub expiry: u32,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct ICXMakeOffer {
    pub order_tx: Txid,
    pub amount: i64,
    pub owner_address: ScriptBuf,
    pub receive_pubkey: Maybe<CompactVec<u8>>,
    pub expiry: u32,
    pub taker_fee: u64,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct ICXSubmitDFCHTLC {
    pub offer_tx: Txid,
    pub amount: i64,
    pub hash: Txid,
    pub timeout: u32,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct ICXSubmitEXTHTLC {
    pub offer_tx: Txid,
    pub amount: i64,
    pub hash: Txid,
    pub htlc_script_address: String,
    pub owner_pubkey: CompactVec<u8>,
    pub timeout: u32,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct ICXClaimDFCHTLC {
    pub dfc_htlc_tx: Txid,
    pub seed: Vec<u8>,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct ICXCloseOrder {
    pub order_tx: Txid,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct ICXCloseOffer {
    pub offer_tx: Txid,
}
