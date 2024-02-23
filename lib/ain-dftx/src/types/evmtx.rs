use ain_macros::ConsensusEncoding;
use bitcoin::{impl_consensus_encoding, io};

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct EvmTx {
    pub raw: Vec<u8>,
}
