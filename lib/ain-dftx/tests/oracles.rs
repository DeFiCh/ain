use ain_dftx::{
    types::{
        oracles::{RemoveOracle, SetOracleData},
        price::{TokenAmount, TokenPrice},
    },
    DfTx, COIN,
};
use ain_macros::test_dftx_serialization;
use bitcoin::consensus::deserialize;

#[test]
#[test_dftx_serialization]
fn test_set_oracle_data() {
    let fixtures = [
        ("6a414466547879061d35948925528b2025c4b84ea6f4899bab6efbcaf63776258186d7728424d1bc29a7600000000001055445534c41010355534400e1f50500000000",
        SetOracleData {
            oracle_id: "d1248472d78681257637f6cafb6eab9b89f4a64eb8c425208b52258994351d06"
                .parse()
                .unwrap(),
            timestamp: 1621567932,
            token_prices: vec![
                TokenPrice {
                  token: String::from("TESLA"),
                  prices: vec![
                    TokenAmount {
                      currency: String::from("USD"),
                      amount: COIN
                    }
                  ].into()
                }
            ].into()
        }
        )];

    for (raw, expected_data) in fixtures {
        let hex = hex::decode(raw).unwrap();
        let dftx = deserialize::<DfTx>(&hex[2..]).unwrap();
        assert!(std::matches!(dftx, DfTx::SetOracleData(_)));

        if let DfTx::SetOracleData(data) = dftx {
            assert_eq!(data, expected_data);
        }
    }
}

#[test]
#[test_dftx_serialization]
fn test_remove_oracle() {
    let fixtures = [(
        "6a254466547868061d35948925528b2025c4b84ea6f4899bab6efbcaf63776258186d7728424d1",
        RemoveOracle {
            oracle_id: "d1248472d78681257637f6cafb6eab9b89f4a64eb8c425208b52258994351d06"
                .parse()
                .unwrap(),
        },
    )];

    for (raw, expected_data) in fixtures {
        let hex = hex::decode(raw).unwrap();
        let dftx = deserialize::<DfTx>(&hex[2..]).unwrap();
        assert!(std::matches!(dftx, DfTx::RemoveOracle(_)));

        if let DfTx::RemoveOracle(data) = dftx {
            assert_eq!(data, expected_data);
        }
    }
}

#[test]
#[test_dftx_serialization]
fn test_appoint_oracle() {}

#[test]
#[test_dftx_serialization]
fn test_update_oracle() {}
