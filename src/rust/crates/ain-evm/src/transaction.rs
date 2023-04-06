use ain_utils::{public_key_to_address, recover_public_key};
use ethereum::{AccessList, TransactionAction, TransactionSignature, TransactionV2};
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

    pub fn sign(&self, key: &H256, chain_id: u64) -> ethereum::LegacyTransaction {
        self.sign_with_chain_id(key, chain_id)
    }

    pub fn sign_with_chain_id(&self, key: &H256, chain_id: u64) -> ethereum::LegacyTransaction {
        let hash = self.signing_hash(chain_id);
        let msg = libsecp256k1::Message::parse(hash.as_fixed_bytes());
        let s = libsecp256k1::sign(
            &msg,
            &libsecp256k1::SecretKey::parse_slice(&key[..]).unwrap(),
        );
        let sig = s.0.serialize();

        let sig = TransactionSignature::new(
            s.1.serialize() as u64 % 2 + chain_id * 2 + 35,
            H256::from_slice(&sig[0..32]),
            H256::from_slice(&sig[32..64]),
        )
        .unwrap();

        ethereum::LegacyTransaction {
            nonce: self.nonce,
            gas_price: self.gas_price,
            gas_limit: self.gas_limit,
            action: self.action,
            value: self.value,
            input: self.input.clone(),
            signature: sig,
        }
    }
}

impl From<&ethereum::LegacyTransaction> for LegacyUnsignedTransaction {
    fn from(src: &ethereum::LegacyTransaction) -> LegacyUnsignedTransaction {
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
    type Error = libsecp256k1::Error;

    fn try_from(src: TransactionV2) -> Result<Self, Self::Error> {
        let pubkey = match &src {
            TransactionV2::Legacy(tx) => recover_public_key(
                &tx.hash(),
                tx.signature.r(),
                tx.signature.s(),
                tx.signature.standard_v(),
            ),
            TransactionV2::EIP2930(tx) => {
                recover_public_key(&tx.hash(), &tx.r, &tx.s, tx.odd_y_parity as u8)
            }
            TransactionV2::EIP1559(tx) => {
                recover_public_key(&tx.hash(), &tx.r, &tx.s, tx.odd_y_parity as u8)
            }
        }?;
        Ok(SignedTx {
            transaction: src,
            sender: public_key_to_address(&pubkey),
            pubkey,
        })
    }
}

use anyhow::anyhow;
use hex::FromHex;

impl TryFrom<&str> for SignedTx {
    type Error = Box<dyn std::error::Error>;

    fn try_from(src: &str) -> Result<Self, Self::Error> {
        let buffer = <Vec<u8>>::from_hex(src)?;
        let tx: TransactionV2 = ethereum::EnvelopedDecodable::decode(&buffer)
            .map_err(|_| anyhow!("Error: decoding raw tx to TransactionV2"))?;

        tx.try_into().map_err(|e: libsecp256k1::Error| e.into())
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
