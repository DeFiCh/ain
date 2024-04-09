#[cfg(test)]
mod tests {
    use std::{str::FromStr, sync::Arc};

    use ain_dftx::{common::CompactVec, price::CurrencyPair, types::oracles::AppointOracle};
    use bitcoin::{BlockHash, ScriptBuf, Txid};
    use defichain_rpc::json::blockchain::Transaction;
    use tempfile::tempdir;

    use crate::{
        indexer::{Context, Index},
        model::BlockContext,
        storage::ocean_store,
        Services,
    };

    #[test]
    fn test_index_appoint_oracle() {
        let temp_dir = tempdir().expect("Failed to create temporary directory");
        let path = temp_dir.path();
        let ocean_store_result = ocean_store::OceanStore::new(path);
        let ocean_store = match ocean_store_result {
            Ok(ocean_store) => Arc::new(ocean_store),
            Err(error) => {
                panic!("Failed to create OceanStore: {}", error);
            }
        };

        let services = Arc::new(Services::new(ocean_store));

        let block_context = BlockContext {
            hash: BlockHash::from_str(
                "0000000000000000000076b72aeedd65368d07d945a6321916a7e8fc4e3babb0",
            )
            .unwrap(),
            height: 10,
            time: 123456789,
            median_time: 123456789,
        };

        let transaction = Transaction {
            txid: Txid::from_str(
                "f42b38ac12e3fafc96ba1a9ba70cbfe326744aef75df5fb9db5d6e2855ca415f",
            )
            .unwrap(),
            hash: "transaction_hash".to_string(),
            version: 1,
            size: 100,
            vsize: 90,
            weight: 80,
            locktime: 0,
            vin: vec![],
            vout: vec![],
            hex: "transaction_hex".to_string(),
        };

        let appoint_oracle = AppointOracle {
            script: ScriptBuf::default(), // Provide a valid script
            weightage: 1,
            price_feeds: CompactVec::from(vec![
                CurrencyPair {
                    token: "BTC".to_string(),
                    currency: "USD".to_string(),
                },
                CurrencyPair {
                    token: "ETH".to_string(),
                    currency: "USD".to_string(),
                },
            ]),
        };
        let ctx = &Context {
            block: block_context,
            tx: transaction,
            tx_idx: 2,
        };
        let result = appoint_oracle.index(&services, ctx);
        assert!(result.is_ok());
        // Add assertions to check if data is stored correctly in the database
    }

    #[test]
    fn test_index_with_invalid_input() {
        // Test with invalid input data, like an empty script or invalid weightage
    }

    #[test]
    fn test_index_error_handling() {
        // Test error handling scenarios, like database errors
    }
}
