use ain_macros::ConsensusEncoding;
use bitcoin::io;

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct EvmTx {
    pub raw: Vec<u8>,
}
