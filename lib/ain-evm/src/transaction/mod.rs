pub mod cache;
pub mod system;

use anyhow::format_err;
use ethereum::{
    AccessList, EnvelopedDecoderError, EnvelopedEncodable, LegacyTransaction, TransactionAction,
    TransactionSignature, TransactionV2,
};
use ethereum_types::{H160, H256, U256};
use rlp::RlpStream;
use sha3::Digest;

use crate::{
    ecrecover::{public_key_to_address, recover_public_key},
    EVMError,
};

// Lowest acceptable value for r and s in sig.
pub const LOWER_H256: H256 = H256([
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
]);

#[derive(Clone, Debug)]
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

    pub fn sign(&self, key: &[u8], chain_id: u64) -> Result<LegacyTransaction, TransactionError> {
        self.sign_with_chain_id(key, chain_id)
    }

    pub fn sign_with_chain_id(
        &self,
        key: &[u8],
        chain_id: u64,
    ) -> Result<LegacyTransaction, TransactionError> {
        let hash = self.signing_hash(chain_id);
        let msg = libsecp256k1::Message::parse(hash.as_fixed_bytes());
        let s = libsecp256k1::sign(&msg, &libsecp256k1::SecretKey::parse_slice(key)?);
        let sig = s.0.serialize();

        let sig = TransactionSignature::new(
            u64::from(s.1.serialize()) % 2 + chain_id * 2 + 35,
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

#[derive(Clone, PartialEq, Eq)]
pub struct SignedTx {
    pub transaction: TransactionV2,
    pub sender: H160,
    hash_cache: Cell<Option<H256>>,
}

impl fmt::Debug for SignedTx {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("SignedTx")
            .field("hash", &self.hash())
            .field("nonce", &self.nonce())
            .field("to", &self.to())
            .field("action", &self.action())
            .field("value", &self.value())
            .field("gas_price", &self.gas_price())
            .field("gas_limit", &self.gas_limit())
            .field("max_fee_per_gas", &self.max_fee_per_gas())
            .field("max_priority_fee_per_gas", &self.max_priority_fee_per_gas())
            .field("access_list", &self.access_list())
            .field("input", &hex::encode(self.data()))
            .field("sender", &self.sender)
            .finish()
    }
}

impl TryFrom<TransactionV2> for SignedTx {
    type Error = TransactionError;

    fn try_from(src: TransactionV2) -> Result<Self, Self::Error> {
        let pubkey = match &src {
            TransactionV2::Legacy(tx) => {
                let msg = ethereum::LegacyTransactionMessage {
                    nonce: tx.nonce,
                    gas_price: tx.gas_price,
                    gas_limit: tx.gas_limit,
                    action: tx.action,
                    value: tx.value,
                    input: tx.input.clone(),
                    chain_id: tx.signature.chain_id(),
                };
                let signing_message = libsecp256k1::Message::parse_slice(&msg.hash()[..])?;
                let hash = H256::from(signing_message.serialize());
                recover_public_key(
                    &hash,
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
                    access_list: tx.access_list.clone(),
                };
                let signing_message = libsecp256k1::Message::parse_slice(&msg.hash()[..])?;
                let hash = H256::from(signing_message.serialize());
                recover_public_key(&hash, &tx.r, &tx.s, u8::from(tx.odd_y_parity))
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
                    access_list: tx.access_list.clone(),
                };
                let signing_message = libsecp256k1::Message::parse_slice(&msg.hash()[..])?;
                let hash = H256::from(signing_message.serialize());
                recover_public_key(&hash, &tx.r, &tx.s, u8::from(tx.odd_y_parity))
            }
        }?;
        Ok(SignedTx {
            transaction: src,
            sender: public_key_to_address(&pubkey),
            hash_cache: Cell::new(None),
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
        match self.action() {
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
            TransactionV2::EIP1559(tx) => tx.max_fee_per_gas,
        }
    }

    pub fn effective_gas_price(&self, base_fee: U256) -> Result<U256, EVMError> {
        match &self.transaction {
            TransactionV2::Legacy(tx) => Ok(tx.gas_price),
            TransactionV2::EIP2930(tx) => Ok(tx.gas_price),
            TransactionV2::EIP1559(tx) => Ok(min(
                tx.max_fee_per_gas,
                tx.max_priority_fee_per_gas
                    .checked_add(base_fee)
                    .ok_or_else(|| format_err!("effective_gas_price overflow"))?,
            )),
        }
    }

    pub fn effective_priority_fee_per_gas(&self, base_fee: U256) -> Result<U256, EVMError> {
        match &self.transaction {
            TransactionV2::Legacy(tx) => Ok(tx.gas_price.checked_sub(base_fee).unwrap_or_default()),
            TransactionV2::EIP2930(tx) => {
                Ok(tx.gas_price.checked_sub(base_fee).unwrap_or_default())
            }
            TransactionV2::EIP1559(tx) => {
                let max_priority_fee = tx.max_fee_per_gas.checked_sub(base_fee).unwrap_or_default();
                Ok(min(tx.max_priority_fee_per_gas, max_priority_fee))
            }
        }
    }

    pub fn data(&self) -> &[u8] {
        match &self.transaction {
            TransactionV2::Legacy(tx) => tx.input.as_ref(),
            TransactionV2::EIP2930(tx) => tx.input.as_ref(),
            TransactionV2::EIP1559(tx) => tx.input.as_ref(),
        }
    }

    pub fn v(&self) -> u64 {
        match &self.transaction {
            TransactionV2::Legacy(tx) => tx.signature.v(),
            TransactionV2::EIP2930(tx) => u64::from(tx.odd_y_parity),
            TransactionV2::EIP1559(tx) => u64::from(tx.odd_y_parity),
        }
    }

    pub fn r(&self) -> H256 {
        match &self.transaction {
            TransactionV2::Legacy(tx) => *tx.signature.r(),
            TransactionV2::EIP2930(tx) => tx.r,
            TransactionV2::EIP1559(tx) => tx.r,
        }
    }

    pub fn s(&self) -> H256 {
        match &self.transaction {
            TransactionV2::Legacy(tx) => *tx.signature.s(),
            TransactionV2::EIP2930(tx) => tx.s,
            TransactionV2::EIP1559(tx) => tx.s,
        }
    }

    pub fn max_fee_per_gas(&self) -> Option<U256> {
        match &self.transaction {
            TransactionV2::Legacy(_) | TransactionV2::EIP2930(_) => None,
            TransactionV2::EIP1559(tx) => Some(tx.max_fee_per_gas),
        }
    }

    pub fn max_priority_fee_per_gas(&self) -> Option<U256> {
        match &self.transaction {
            TransactionV2::Legacy(_) | TransactionV2::EIP2930(_) => None,
            TransactionV2::EIP1559(tx) => Some(tx.max_priority_fee_per_gas),
        }
    }

    pub fn chain_id(&self) -> u64 {
        match &self.transaction {
            TransactionV2::Legacy(tx) => tx.signature.chain_id().unwrap_or_default(),
            TransactionV2::EIP2930(tx) => tx.chain_id,
            TransactionV2::EIP1559(tx) => tx.chain_id,
        }
    }

    pub fn hash(&self) -> H256 {
        let h = &self.hash_cache;
        if h.get().is_none() {
            let val = match &self.transaction {
                TransactionV2::Legacy(tx) => tx.hash(),
                TransactionV2::EIP2930(tx) => tx.hash(),
                TransactionV2::EIP1559(tx) => tx.hash(),
            };
            h.set(Some(val));
        }
        h.get().unwrap()
    }

    pub fn get_tx_type(&self) -> U256 {
        U256::from(EnvelopedEncodable::type_id(&self.transaction).unwrap_or_default())
    }
}

use std::{
    cell::Cell,
    cmp::min,
    convert::{TryFrom, TryInto},
    fmt,
};

#[derive(Debug)]
pub enum TransactionError {
    Secp256k1Error(libsecp256k1::Error),
    DecodingError,
    SignatureError,
    FromHexError(hex::FromHexError),
    ConstructionError,
}

impl fmt::Display for TransactionError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match *self {
            TransactionError::Secp256k1Error(ref e) => write!(f, "Secp256k1 error: {e}"),
            TransactionError::DecodingError => {
                write!(f, "Error decoding raw transaction")
            }
            TransactionError::SignatureError => {
                write!(f, "Error creating new signature")
            }
            TransactionError::FromHexError(ref e) => {
                write!(f, "Error parsing hex: {e}")
            }
            TransactionError::ConstructionError => {
                write!(f, "Error constructing Transaction")
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

#[cfg(test)]
mod tests {
    use std::{error::Error, fs, path::Path};

    use ethereum::{AccessListItem, EnvelopedEncodable};
    use ethereum_types::{H160, H256, U256, U64};
    use serde::Deserialize;

    use crate::{bytes::Bytes, transaction::SignedTx};

    #[test]
    fn test_signed_tx_from_raw_tx() {
        // Legacy
        let signed_tx = crate::transaction::SignedTx::try_from("f86b8085689451eee18252089434c1ca09a2dc717d89baef2f30ff6a6b2975e17e872386f26fc10000802da0ae5c76f8073460cbc7a911d3cc1b367072db64848a9532343559ce6917c51a46a01d2e4928450c59acca3de8340eb15b7446b37936265a51ab35e63f749a048002").unwrap();

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

    #[derive(Deserialize, Debug)]
    #[serde(rename_all = "camelCase")]
    struct ExpectedTx {
        hash: H256,
        from: H160,
        to: Option<H160>,
        gas: U256,
        gas_price: U256,
        value: U256,
        input: Bytes,
        nonce: U256,
        v: U64,
        r: H256,
        s: H256,
        _block_hash: H256,
        _block_number: U256,
        _transaction_index: U256,
        #[serde(rename = "type")]
        r#type: U64,
        max_fee_per_gas: Option<U256>,
        max_priority_fee_per_gas: Option<U256>,
        #[serde(default)]
        access_list: Vec<AccessListItem>,
        chain_id: Option<U64>,
    }

    #[cfg(test)]
    fn get_test_data(path: &Path) -> Result<Vec<(String, ExpectedTx)>, Box<dyn Error>> {
        let content = fs::read_to_string(path)?;

        let r = serde_json::from_str(&content)?;
        Ok(r)
    }

    fn assert_raw_hash_matches_expected_tx((raw_hash, tx): (String, ExpectedTx)) {
        let signed_tx: SignedTx = (&raw_hash[2..]).try_into().unwrap();

        assert_eq!(signed_tx.sender, tx.from);
        assert_eq!(signed_tx.gas_limit(), tx.gas);
        assert_eq!(signed_tx.to(), tx.to);
        assert_eq!(signed_tx.hash(), tx.hash);
        assert_eq!(signed_tx.data().to_vec(), tx.input.0);
        assert_eq!(signed_tx.nonce(), tx.nonce);
        assert_eq!(signed_tx.value(), tx.value);
        assert_eq!(signed_tx.access_list(), tx.access_list);
        assert_eq!(signed_tx.max_fee_per_gas(), tx.max_fee_per_gas);
        assert_eq!(
            signed_tx.max_priority_fee_per_gas(),
            tx.max_priority_fee_per_gas
        );
        assert_eq!(U64::from(signed_tx.v()), tx.v);
        assert_eq!(signed_tx.r(), tx.r);
        assert_eq!(signed_tx.s(), tx.s);

        if let Some(chain_id) = tx.chain_id {
            assert_eq!(U64::from(signed_tx.chain_id()), chain_id);
        }

        let r#type =
            U64::from(EnvelopedEncodable::type_id(&signed_tx.transaction).unwrap_or_default());
        assert_eq!(r#type, tx.r#type);

        // Can't get gas_price without block base fee
        if r#type == U64::zero() || r#type == U64::one() {
            assert_eq!(signed_tx.gas_price(), tx.gas_price);
        }
    }

    #[test]
    fn test_transfer_txs() -> Result<(), Box<dyn Error>> {
        get_test_data(Path::new("./testdata/transfer_txs.json"))?
            .into_iter()
            .for_each(assert_raw_hash_matches_expected_tx);
        Ok(())
    }

    #[test]
    fn test_call_smart_contract_txs() -> Result<(), Box<dyn Error>> {
        get_test_data(Path::new("./testdata/call_smart_contract_txs.json"))?
            .into_iter()
            .for_each(assert_raw_hash_matches_expected_tx);
        Ok(())
    }

    #[test]
    fn test_deploy_smart_contract_txs() -> Result<(), Box<dyn Error>> {
        get_test_data(Path::new("./testdata/deploy_smart_contract_txs.json"))?
            .into_iter()
            .for_each(assert_raw_hash_matches_expected_tx);
        Ok(())
    }

    #[test]
    fn test_tx_type() -> Result<(), Box<dyn Error>> {
        let raw_legacy_tx = "0xf90152808522ecb25c008307a1208080b8fe608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c634300080200338208fda0f6d889435dbfe9ea49b984fe1cab94cae59fa774b602256abc9f657c2abdd5bea0609176ffd6023896913cd9e12b61a004a970187cc53623ef49afb0417cb44929";
        let raw_eip2930_tx = "0x01f9018d82046d018522ecb25c008307a1208080b8fe608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c63430008020033f838f7949b8a4af42140d8a4c153a822f02571a1dd037e89e1a0000000000000000000000000000000000000000000000000000000000000000001a0f69240ab7127d845ad35a450d377f9d7d81c3e9d350b12bf53d14dc44a9e7bf2a067c2e6d1d30f20c670fbb9b79296c26ff372086f2ca3aead851bcc3510b638a7";
        let raw_eip1559_tx = "0x02f9015a82046d02852363e7f0008522ecb25c008307a1208080b8fe608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c63430008020033c080a05eceb8ff0ce33c95106051d82896aaf41bc899f625189922d12edeb7ce6fd56da07c37f48b1f4dfbf89a81cbe62c93ab23eec00808f7daca0cc5cf29860d112759";

        // Test legacy tx
        let signed_tx: SignedTx = (raw_legacy_tx[2..]).try_into().unwrap();
        assert_eq!(signed_tx.get_tx_type(), U256::zero());

        // Test EIP2930 tx
        let signed_tx: SignedTx = (raw_eip2930_tx[2..]).try_into().unwrap();
        assert_eq!(signed_tx.get_tx_type(), U256::from(1));

        // Test EIP1559 tx
        let signed_tx: SignedTx = (raw_eip1559_tx[2..]).try_into().unwrap();
        assert_eq!(signed_tx.get_tx_type(), U256::from(2));
        Ok(())
    }
}
