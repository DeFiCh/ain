use ain_dftx::{deserialize, Block, DfTx};

#[test]
fn test_block() {
    let s = std::fs::read_to_string("./tests/data/block.txt").unwrap();

    for line in s.lines() {
        let l = line.split(' ').next().unwrap();
        let hex = hex::decode(l).unwrap();
        let block = deserialize::<Block>(&hex).unwrap();

        for tx in block.txdata {
            let bytes = tx.output[0].clone().script_pubkey.into_bytes();
            if bytes.len() > 2 && bytes[0] == 0x6a && bytes[1] <= 0x4e {
                let offset = 1 + match bytes[1] {
                    0x4c => 2,
                    0x4d => 3,
                    0x4e => 4,
                    _ => 1,
                };

                let raw_tx = &bytes[offset..];

                let tx = deserialize::<DfTx>(raw_tx).unwrap();
                println!("tx : {:?}", tx);
            }
        }
    }
}
