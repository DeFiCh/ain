use crate::ecrecover::{public_key_to_address, recover_public_key};
use ethereum::{
    AccessList, EnvelopedDecoderError, LegacyTransaction, TransactionAction, TransactionSignature,
    TransactionV2,
};
use libsecp256k1::PublicKey;
use primitive_types::{H160, H256, U256};

use rlp::RlpStream;
use sha3::Digest;

// Lowest acceptable value for r and s in sig.
pub const LOWER_H256: H256 = H256([
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
]);

pub struct LegacyUnsignedTransaction {
    pub nonce: U256,
    pub gas_price: U256,
    pub gas_limit: U256,
    pub action: TransactionAction,
    pub value: U256,
    pub input: Vec<u8>,
    pub sig: TransactionSignature,
}

impl LegacyUnsignedTransaction {
    fn signing_rlp_append(&self, s: &mut RlpStream, chain_id: u64) {
        s.begin_list(9);
        s.append(&self.nonce);
        s.append(&self.gas_price);
        s.append(&self.gas_limit);
        s.append(&self.action);
        s.append(&self.value);
        s.append(&self.input);
        s.append(&chain_id);
        s.append(&0u8);
        s.append(&0u8);
    }

    fn signing_hash(&self, chain_id: u64) -> H256 {
        let mut stream = RlpStream::new();
        self.signing_rlp_append(&mut stream, chain_id);
        let mut output = [0u8; 32];
        output.copy_from_slice(sha3::Keccak256::digest(&stream.out()).as_slice());
        H256::from(output)
    }

    pub fn sign(&self, key: &H256, chain_id: u64) -> Result<LegacyTransaction, TransactionError> {
        self.sign_with_chain_id(key, chain_id)
    }

    pub fn sign_with_chain_id(
        &self,
        key: &H256,
        chain_id: u64,
    ) -> Result<LegacyTransaction, TransactionError> {
        let hash = self.signing_hash(chain_id);
        let msg = libsecp256k1::Message::parse(hash.as_fixed_bytes());
        let s = libsecp256k1::sign(&msg, &libsecp256k1::SecretKey::parse_slice(&key[..])?);
        let sig = s.0.serialize();

        let sig = TransactionSignature::new(
            s.1.serialize() as u64 % 2 + chain_id * 2 + 35,
            H256::from_slice(&sig[0..32]),
            H256::from_slice(&sig[32..64]),
        )
        .ok_or(TransactionError::SignatureError)?;

        Ok(LegacyTransaction {
            nonce: self.nonce,
            gas_price: self.gas_price,
            gas_limit: self.gas_limit,
            action: self.action,
            value: self.value,
            input: self.input.clone(),
            signature: sig,
        })
    }
}

impl From<&LegacyTransaction> for LegacyUnsignedTransaction {
    fn from(src: &LegacyTransaction) -> LegacyUnsignedTransaction {
        LegacyUnsignedTransaction {
            nonce: src.nonce,
            gas_price: src.gas_price,
            gas_limit: src.gas_limit,
            action: src.action,
            value: src.value,
            input: src.input.clone(),
            sig: src.signature.clone(),
        }
    }
}

#[derive(Clone, Debug)]
pub struct SignedTx {
    pub transaction: TransactionV2,
    pub sender: H160,
    pub pubkey: PublicKey,
}

impl TryFrom<TransactionV2> for SignedTx {
    type Error = TransactionError;

    fn try_from(src: TransactionV2) -> Result<Self, Self::Error> {
        let pubkey = match &src {
            TransactionV2::Legacy(tx) => {
                let t = LegacyUnsignedTransaction {
                    nonce: tx.nonce,
                    gas_price: tx.gas_price,
                    gas_limit: tx.gas_limit,
                    action: tx.action,
                    value: tx.value,
                    input: tx.input.clone(),
                    sig: tx.signature.clone(),
                };

                recover_public_key(
                    &t.signing_hash(t.sig.chain_id().unwrap()),
                    tx.signature.r(),
                    tx.signature.s(),
                    tx.signature.standard_v(),
                )
            }
            TransactionV2::EIP2930(tx) => {
                let msg = ethereum::EIP2930TransactionMessage {
                    chain_id: tx.chain_id,
                    nonce: tx.nonce,
                    gas_price: tx.gas_price,
                    gas_limit: tx.gas_limit,
                    action: tx.action,
                    value: tx.value,
                    input: tx.input.clone(),
                    access_list: vec![],
                };
                let signing_message = libsecp256k1::Message::parse_slice(&msg.hash()[..]).unwrap();
                let hash = H256::from(signing_message.serialize());
                recover_public_key(&hash, &tx.r, &tx.s, tx.odd_y_parity as u8)
            }
            TransactionV2::EIP1559(tx) => {
                let msg = ethereum::EIP1559TransactionMessage {
                    chain_id: tx.chain_id,
                    nonce: tx.nonce,
                    max_priority_fee_per_gas: tx.max_priority_fee_per_gas,
                    max_fee_per_gas: tx.max_fee_per_gas,
                    gas_limit: tx.gas_limit,
                    action: tx.action,
                    value: tx.value,
                    input: tx.input.clone(),
                    access_list: vec![],
                };
                let signing_message = libsecp256k1::Message::parse_slice(&msg.hash()[..]).unwrap();
                let hash = H256::from(signing_message.serialize());
                recover_public_key(&hash, &tx.r, &tx.s, tx.odd_y_parity as u8)
            }
        }?;
        Ok(SignedTx {
            transaction: src,
            sender: public_key_to_address(&pubkey),
            pubkey,
        })
    }
}

use hex::FromHex;

impl TryFrom<&str> for SignedTx {
    type Error = TransactionError;

    fn try_from(src: &str) -> Result<Self, Self::Error> {
        let buffer = <Vec<u8>>::from_hex(src)?;
        let tx: TransactionV2 = ethereum::EnvelopedDecodable::decode(&buffer)?;

        tx.try_into()
    }
}

impl SignedTx {
    pub fn nonce(&self) -> U256 {
        match &self.transaction {
            TransactionV2::Legacy(t) => t.nonce,
            TransactionV2::EIP2930(t) => t.nonce,
            TransactionV2::EIP1559(t) => t.nonce,
        }
    }

    pub fn to(&self) -> Option<H160> {
        let action = self.action();
        match action {
            TransactionAction::Call(to) => Some(to),
            TransactionAction::Create => None,
        }
    }

    pub fn action(&self) -> TransactionAction {
        match &self.transaction {
            TransactionV2::Legacy(t) => t.action,
            TransactionV2::EIP2930(t) => t.action,
            TransactionV2::EIP1559(t) => t.action,
        }
    }

    pub fn access_list(&self) -> AccessList {
        match &self.transaction {
            TransactionV2::Legacy(_) => Vec::new(),
            TransactionV2::EIP2930(tx) => tx.access_list.clone(),
            TransactionV2::EIP1559(tx) => tx.access_list.clone(),
        }
    }

    pub fn value(&self) -> U256 {
        match &self.transaction {
            TransactionV2::Legacy(tx) => tx.value,
            TransactionV2::EIP2930(tx) => tx.value,
            TransactionV2::EIP1559(tx) => tx.value,
        }
    }

    pub fn gas_limit(&self) -> U256 {
        match &self.transaction {
            TransactionV2::Legacy(tx) => tx.gas_limit,
            TransactionV2::EIP2930(tx) => tx.gas_limit,
            TransactionV2::EIP1559(tx) => tx.gas_limit,
        }
    }

    pub fn gas_price(&self) -> U256 {
        match &self.transaction {
            TransactionV2::Legacy(tx) => tx.gas_price,
            TransactionV2::EIP2930(tx) => tx.gas_price,
            TransactionV2::EIP1559(tx) => tx.max_fee_per_gas.min(tx.max_priority_fee_per_gas), // TODO verify calculation
        }
    }

    pub fn data(&self) -> &[u8] {
        match &self.transaction {
            TransactionV2::Legacy(tx) => tx.input.as_ref(),
            TransactionV2::EIP2930(tx) => tx.input.as_ref(),
            TransactionV2::EIP1559(tx) => tx.input.as_ref(),
        }
    }
}

use std::convert::{TryFrom, TryInto};
use std::fmt;

#[derive(Debug)]
pub enum TransactionError {
    Secp256k1Error(libsecp256k1::Error),
    DecodingError,
    SignatureError,
    FromHexError(hex::FromHexError),
}

impl fmt::Display for TransactionError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match *self {
            TransactionError::Secp256k1Error(ref e) => write!(f, "Secp256k1 error: {}", e),
            TransactionError::DecodingError => {
                write!(f, "Error decoding raw transaction")
            }
            TransactionError::SignatureError => {
                write!(f, "Error creating new signature")
            }
            TransactionError::FromHexError(ref e) => {
                write!(f, "Error parsing hex: {}", e)
            }
        }
    }
}

impl std::error::Error for TransactionError {}

use std::convert::From;

impl From<libsecp256k1::Error> for TransactionError {
    fn from(e: libsecp256k1::Error) -> Self {
        TransactionError::Secp256k1Error(e)
    }
}

impl From<hex::FromHexError> for TransactionError {
    fn from(e: hex::FromHexError) -> Self {
        TransactionError::FromHexError(e)
    }
}

impl<T> From<EnvelopedDecoderError<T>> for TransactionError {
    fn from(_: EnvelopedDecoderError<T>) -> Self {
        TransactionError::DecodingError
    }
}

mod tests {

    #[test]
    fn test_signed_tx_from_raw_tx() {
        // Legacy
        let signed_tx = crate::transaction::SignedTx::try_from("f86b8085689451eee18252089434c1ca09a2dc717d89baef2f30ff6a6b2975e17e872386f26fc10000802da0ae5c76f8073460cbc7a911d3cc1b367072db64848a9532343559ce6917c51a46a01d2e4928450c59acca3de8340eb15b7446b37936265a51ab35e63f749a048002").unwrap();

        assert_eq!(hex::encode(signed_tx.pubkey.serialize()), "044c6412f7cd3ac0e2538c3c9843d27d1e03b422eaf655c6a699da22b57a89802989318dbaeea62f5fc751fa8cd1404e687d67b8ab8513fe0d37bafbf407aa6cf7");
        assert_eq!(
            hex::encode(signed_tx.sender.as_fixed_bytes()),
            "f829754bae400b679febefdcfc9944c323e1f94e"
        );

        // EIP-1559
        let signed_tx = crate::transaction::SignedTx::try_from("02f871018302fe7f80850735ebc84f827530942f777e9f26aa138ed21c404079e80656b448c7118774ab8279a9e27380c001a0f97db05e9814734c6d7bcca5ce644fc1b780c28e8617eec4a3142712777cabe7a01ad8667f28d7cc1b2e0884340c67d73644fac314da4bab3bfc068cf00c622774").unwrap();

        assert_eq!(
            hex::encode(signed_tx.sender.as_fixed_bytes()),
            "95222290dd7278aa3ddd389cc1e1d165cc4bafe5"
        );

        // EIP-2930
        let signed_tx = crate::transaction::SignedTx::try_from("01f86d050185689451eee18252089434c1ca09a2dc717d89baef2f30ff6a6b2975e17e872386f26fc1000080c080a0632502442f6bd0dbe14c087798277ce04bdede53c4642559a0a7d7e20fc7e8f1a0517c7504cb9adfe67f58dd43e00e77b4b2159e9f2c378b7616ba30dfa711ec8f").unwrap();

        assert_eq!(
            hex::encode(signed_tx.sender.as_fixed_bytes()),
            "f829754bae400b679febefdcfc9944c323e1f94e"
        );
    }
}
