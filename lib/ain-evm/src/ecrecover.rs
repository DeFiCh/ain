use ethereum_types::{H160, H256};
use libsecp256k1::{Error, PublicKey, RecoveryId, Signature};
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

#[cfg(test)]
mod tests {
    use ethereum_types::*;
    use hex_literal::hex;

    use super::{public_key_to_address, recover_public_key};

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

    #[test]
    fn test_invalid_recovery() {
        let empty = [0; 32];
        let one_vec = [1; 32];
        let zero = H256::from_slice(&empty[..]);
        let one = H256::from_slice(&one_vec[..]);

        let recovery_id = 0;

        assert!(recover_public_key(&zero, &zero, &zero, recovery_id).is_err());
        assert!(recover_public_key(&one, &zero, &zero, recovery_id).is_err());
        assert!(recover_public_key(&one, &one, &zero, recovery_id).is_err());
        assert!(recover_public_key(&zero, &zero, &one, recovery_id).is_err());
        // let valids = [
        //     recover_public_key(&zero, &one, &one, recovery_id),
        //     recover_public_key(&one, &one, &one, recovery_id),
        // ];
        // for x in valids {
        //     assert!(x.is_ok());
        //     let pub_key = x.unwrap();
        //     let address = public_key_to_address(&pub_key);
        //     println!("address: {:x}", address);
        // }
    }

    #[test]
    fn _test_recover_test2() {
        // Tx hex: f86c808504e3b29200825208946c34cbb9219d8caa428835d2073e8ec88ba0a110880de0b6b3a76400008025a037f41c543402c9b02b35b45ef43ac31a63dcbeba0c622249810ecdec00aee376a05eb2be77eb0c7a1875a53ba15fc6afe246fbffe869157edbde64270e41ba045e

        // Decoded:
        // {
        //     "nonce": 0,
        //     "gasPrice": 21000000000,
        //     "gasLimit": 21000,
        //     "to": "0x6c34cbb9219d8caa428835d2073e8ec88ba0a110",
        //     "value": 1000000000000000000,
        //     "data": "",
        //     "from": "0x9b8a4af42140d8a4c153a822f02571a1dd037e89",
        //     "r": "37f41c543402c9b02b35b45ef43ac31a63dcbeba0c622249810ecdec00aee376",
        //     "v": "25",
        //     "s": "5eb2be77eb0c7a1875a53ba15fc6afe246fbffe869157edbde64270e41ba045e"
        //  }

        // let _from = "0x9b8a4af42140d8a4c153a822f02571a1dd037e89";
        // let _r = "37f41c543402c9b02b35b45ef43ac31a63dcbeba0c622249810ecdec00aee376";
        // let _s = "5eb2be77eb0c7a1875a53ba15fc6afe246fbffe869157edbde64270e41ba045e";
        // let hash = "";

        // let recovery_id = 0;

        // let pubkey = recover_public_key(&hash, &r, &s, recovery_id);
        // assert!(pubkey.is_ok());
        // let address = public_key_to_address(&pubkey.unwrap());
        // assert_eq!(
        //     address,
        //     from
        // );
    }

    #[test]
    fn test_recover_test3() {
        // Tx: https://etherscan.io/getRawTx?tx=0x89221691a67b15427c97f1fd0cd65966ff617728cd897be27d88a04ee0bc1e2d
        //
        // Hex:
        // 0xf901ed828c848503d77a05008301754a94d9f61a4a96f66afe09c6f55b72aeaf1590ac849580b9018439125215000000000000000000000000260f38dbc414a9d588ca2dedddb7588da25736a60000000000000000000000000000000000000000000000000205061e42dd640000000000000000000000000000000000000000000000000000000000000000c0000000000000000000000000000000000000000000000000000000005dd767960000000000000000000000000000000000000000000000000000000000000c4a000000000000000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000004187e330045ecad46221a46b1be365a45d293988e2162b94ef939d837d49be6cbb0425cfcfa16bed80a8a8fdb0984c24d081781603473e9077db8ff92618c310bd1c000000000000000000000000000000000000000000000000000000000000001ca02fa2191f585d3f704d073dd19fd2dddc745612dacdd60fc27d4df53d2717a057a069bb520bc99dfa007ce23ba52b4eed758d53eb7cc4c66bf7e045c2c13e62675f

        // Decoded with https://flightwallet.github.io/decode-eth-tx/
        // {
        //     "nonce": 35972,
        //     "gasPrice": 16500000000,
        //     "gasLimit": 95562,
        //     "to": "0xd9f61a4a96f66afe09c6f55b72aeaf1590ac8495",
        //     "value": 0,
        //     "data": "39125215000000000000000000000000260f38dbc414a9d588ca2dedddb7588da25736a60000000000000000000000000000000000000000000000000205061e42dd640000000000000000000000000000000000000000000000000000000000000000c0000000000000000000000000000000000000000000000000000000005dd767960000000000000000000000000000000000000000000000000000000000000c4a000000000000000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000004187e330045ecad46221a46b1be365a45d293988e2162b94ef939d837d49be6cbb0425cfcfa16bed80a8a8fdb0984c24d081781603473e9077db8ff92618c310bd1c00000000000000000000000000000000000000000000000000000000000000",
        //     "from": "0x3f1e01f65cac6cdb60ef5f7fc8f988f042949a2c",
        //     "r": "2fa2191f585d3f704d073dd19fd2dddc745612dacdd60fc27d4df53d2717a057",
        //     "v": "1c",
        //     "s": "69bb520bc99dfa007ce23ba52b4eed758d53eb7cc4c66bf7e045c2c13e62675f"
        //   }

        // let to_str = "0xd9f61a4A96f66Afe09c6F55B72AeaF1590AC8495";
        // let txhash_str = "0x89221691a67b15427c97f1fd0cd65966ff617728cd897be27d88a04ee0bc1e2d";
        // let from_str = "0x3f1e01F65Cac6CDB60Ef5F7fC8F988f042949a2C";
        // let s_str = "69bb520bc99dfa007ce23ba52b4eed758d53eb7cc4c66bf7e045c2c13e62675f";
        // let r_str = "2fa2191f585d3f704d073dd19fd2dddc745612dacdd60fc27d4df53d2717a057";

        // let h_vals = &[txhash_str, s_str, r_str].iter().map(|x| H256::).collect::<Vec<_>>();
        // let [hash, r, s ] = h_vals[..];

        let from = H160::from_slice(&hex!("3f1e01F65Cac6CDB60Ef5F7fC8F988f042949a2C"));
        let _to = H160::from_slice(&hex!("d9f61a4A96f66Afe09c6F55B72AeaF1590AC8495"));

        let hash = H256::from_slice(&hex!(
            "89221691a67b15427c97f1fd0cd65966ff617728cd897be27d88a04ee0bc1e2d"
        ));
        let r = H256::from_slice(&hex!(
            "2fa2191f585d3f704d073dd19fd2dddc745612dacdd60fc27d4df53d2717a057"
        ));
        let s = H256::from_slice(&hex!(
            "69bb520bc99dfa007ce23ba52b4eed758d53eb7cc4c66bf7e045c2c13e62675f"
        ));

        for x in 0..=4 {
            let rx = x;
            let pubkey = recover_public_key(&hash, &r, &s, rx);
            if let Ok(pubkey) = pubkey {
                let address = public_key_to_address(&pubkey);
                println!("address: {:x}", address);
                println!("from: {:x}", from);
                if address == from {
                    println!("found: {}", rx);
                    break;
                }
            } else {
                println!("{:?}", pubkey.err().unwrap());
            }
        }
    }
}
