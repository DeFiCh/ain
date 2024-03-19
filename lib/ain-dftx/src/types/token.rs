use ain_macros::ConsensusEncoding;
use bitcoin::{
    consensus::{Decodable, Encodable, ReadExt},
    io, ScriptBuf, Txid,
};
use bitflags::bitflags;

use super::{balance::TokenBalanceUInt32, common::CompactVec};
use crate::common::Maybe;

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct MintToken {
    pub balances: CompactVec<TokenBalanceUInt32>,
    pub to: Maybe<ScriptBuf>,
}

bitflags! {
    #[derive(Debug, PartialEq, Eq)]
    struct TokenFlags: u8 {
        const NONE = 0;
        const MINTABLE = 0x01;
        const TRADEABLE = 0x02;
        const DAT = 0x04;
        const LPS = 0x08;       // Liquidity Pool Share
        const FINALIZED = 0x10; // locked forever
        const LOANTOKEN = 0x20; // token created for loan
        const DEFAULT = TokenFlags::MINTABLE.bits() | TokenFlags::TRADEABLE.bits();
    }
}

impl Default for TokenFlags {
    fn default() -> Self {
        Self::DEFAULT
    }
}

impl Encodable for TokenFlags {
    fn consensus_encode<W: io::Write + ?Sized>(&self, writer: &mut W) -> Result<usize, io::Error> {
        self.bits().consensus_encode(writer)
    }
}

impl Decodable for TokenFlags {
    fn consensus_decode<R: io::Read + ?Sized>(
        reader: &mut R,
    ) -> Result<Self, bitcoin::consensus::encode::Error> {
        let v = reader.read_u8()?;
        Ok(Self::from_bits_retain(v))
    }
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct CreateToken {
    pub symbol: String,
    pub name: String,
    pub decimal: u8,
    pub limit: i64,
    pub flags: u8,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct UpdateToken {
    pub creation_tx: Txid,
    pub is_dat: bool,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct UpdateTokenAny {
    pub creation_tx: Txid,
    pub token: CreateToken,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct VariantScript {
    pub r#type: u32,
    pub context: ScriptBuf,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct BurnToken {
    pub amounts: CompactVec<TokenBalanceUInt32>,
    pub from: ScriptBuf,
    pub burn_type: u8,
    pub variant_context: VariantScript,
}
