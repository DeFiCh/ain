use libsecp256k1::Error;
use libsecp256k1::{PublicKey, RecoveryId, Signature};
use primitive_types::{H160, H256};
use sha3::Digest;

pub fn recover_public_key(
    hash: &H256,
    r: &H256,
    s: &H256,
    recovery_id: u8,
) -> Result<PublicKey, Error> {
    let msg = libsecp256k1::Message::parse(hash.as_fixed_bytes());

    let mut standard_slice = [0u8; 64];
    standard_slice[..32].copy_from_slice(r.as_fixed_bytes());
    standard_slice[32..].copy_from_slice(s.as_fixed_bytes());
    let signature = Signature::parse_standard_slice(&standard_slice)?;

    let recovery_id = RecoveryId::parse(recovery_id)?;
    libsecp256k1::recover(&msg, &signature, &recovery_id)
}

pub fn public_key_to_address(pubkey: &PublicKey) -> H160 {
    let mut output = [0u8; 32];
    output.copy_from_slice(sha3::Keccak256::digest(&pubkey.serialize()[1..]).as_slice());
    let mut ret = H160::zero();
    ret.as_bytes_mut().copy_from_slice(&output[12..]);
    ret
}

mod tests {
    #[macro_use]
    use hex_literal::hex;

    use libsecp256k1::PublicKey;
    use primitive_types::{H160, H256};

    use super::{public_key_to_address, recover_public_key};
    use hex::FromHex;

    #[test]
    fn test_recover_public_key_and_address() {
        let hash = H256::from_slice(&hex!(
            "d107d8794e181ce28f9d3e50312941a775e137c7b70b64bd9221db6a68ffd1ea"
        ));
        let r = H256::from_slice(&hex!(
            "ae5c76f8073460cbc7a911d3cc1b367072db64848a9532343559ce6917c51a46"
        ));
        let s = H256::from_slice(&hex!(
            "1d2e4928450c59acca3de8340eb15b7446b37936265a51ab35e63f749a048002"
        ));
        let recovery_id = 0;

        let pubkey = recover_public_key(&hash, &r, &s, recovery_id);
        assert!(pubkey.is_ok());
        assert_eq!(
            hex::encode(&pubkey.unwrap().serialize()),
            "044c6412f7cd3ac0e2538c3c9843d27d1e03b422eaf655c6a699da22b57a89802989318dbaeea62f5fc751fa8cd1404e687d67b8ab8513fe0d37bafbf407aa6cf7"
        );
        let address = public_key_to_address(&pubkey.unwrap());
        assert_eq!(
            address,
            "f829754bae400b679febefdcfc9944c323e1f94e".parse().unwrap()
        );
    }
}
