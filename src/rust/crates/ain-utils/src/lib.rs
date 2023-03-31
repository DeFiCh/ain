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
    let signature = Signature::parse_standard_slice(&[r.as_bytes(), &s.as_bytes()].concat())?;
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
            "6091be99153563845f8af2ff710a9854a70551a5a6b914a296931444fa360d40"
        ));
        let r = H256::from_slice(&hex!(
            "f53cdd6fad6cb1014486fbc626f0a8109cce5df1529e6070432bb8798642e548"
        ));
        let s = H256::from_slice(&hex!(
            "759484f7adefce95559ed3c07bb179586ceb74bb4aac060dc1f6d6aa58b95a42"
        ));
        let recovery_id = 0;

        let pubkey = recover_public_key(&hash, &r, &s, recovery_id);
        assert!(pubkey.is_ok());
        let address = public_key_to_address(&pubkey.unwrap());
        assert_eq!(
            address,
            "89790061e1efe88bda902193c8ab3b061aa4ef2c".parse().unwrap()
        );
    }
}
