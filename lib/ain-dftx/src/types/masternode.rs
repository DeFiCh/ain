use ain_macros::ConsensusEncoding;
use bitcoin::{
    consensus::{Decodable, Encodable},
    hashes::Hash,
    io, PubkeyHash, Txid, VarInt,
};

use super::common::{CompactVec, Maybe};

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct CreateMasternode {
    pub operator_type: u8,
    pub operator_pub_key_hash: PubkeyHash,
    pub timelock: Maybe<u16>,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct ResignMasternode {
    pub node_id: Txid,
}

#[derive(Debug, PartialEq, Eq)]
pub struct UpdateMasternodeAddress {
    pub r#type: u8,
    pub address_pub_key_hash: Option<PubkeyHash>,
}

impl Encodable for UpdateMasternodeAddress {
    fn consensus_encode<W: io::Write + ?Sized>(&self, writer: &mut W) -> Result<usize, io::Error> {
        let mut len = self.r#type.consensus_encode(writer)?;
        match self.address_pub_key_hash {
            Some(key) => {
                len += 20u8.consensus_encode(writer)?;
                len += writer.write(&key.to_byte_array())?;
            }
            None => {
                len += 0u8.consensus_encode(writer)?;
            }
        }
        Ok(len)
    }
}

impl Decodable for UpdateMasternodeAddress {
    fn consensus_decode<R: io::Read + ?Sized>(
        reader: &mut R,
    ) -> Result<Self, bitcoin::consensus::encode::Error> {
        let r#type = u8::consensus_decode(reader)?;
        let len = VarInt::consensus_decode(reader)?;
        let address_pub_key_hash = if len.0 > 0 {
            Some(PubkeyHash::consensus_decode(reader)?)
        } else {
            None
        };
        Ok(UpdateMasternodeAddress {
            r#type,
            address_pub_key_hash,
        })
    }
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct UpdateMasternodeData {
    pub r#type: u8,
    pub address: UpdateMasternodeAddress,
}

#[derive(ConsensusEncoding, Debug, PartialEq, Eq)]
pub struct UpdateMasternode {
    pub node_id: Txid,
    pub updates: CompactVec<UpdateMasternodeData>,
}
