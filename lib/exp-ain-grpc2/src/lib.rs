pub fn add(left: usize, right: usize) -> usize {
    left + right
}

pub mod proto {
    // This is the target to be generated.
    // include!(concat!(env!("OUT_DIR"), "/eth.rs"));
    include!(concat!(env!("CARGO_MANIFEST_DIR"), "/gen", "/eth.rs"));
}

#[cfg(test)]
mod tests {

    use std::{
        any::{Any, TypeId},
        fmt::Debug,
        marker::PhantomData,
    };

    use serde::Serialize;

    use super::*;
    use crate::proto::*;

    fn print_debug<T: Debug + Serialize>(val: T) {
        println!("{:?}\n{}", val, serde_json::to_string_pretty(&val).unwrap());
    }

    #[test]
    fn json_outputs_basic() {
        let result = add(2, 2);
        assert_eq!(result, 4);

        let mut x = proto::EthCallResponse::default();
        x.data = "hello".to_string();
        print_debug(x);
    }

    #[test]
    fn json_outputs_flattened_enums() {
        let mut x: EthSyncingResponse = proto::EthSyncingResponse::default();
        x.value = Some(eth_syncing_response::Value::Status(true));
        print_debug(x);

        let mut x = proto::EthSyncingResponse::default();
        x.value = Some(eth_syncing_response::Value::SyncInfo(
            EthSyncingInfo::default(),
        ));
        print_debug(x);
    }

    #[test]
    fn json_outputs_transaction_receipts() {
        let result = add(2, 2);
        assert_eq!(result, 4);

        let mut x = proto::EthGetTransactionReceiptResponse::default();
        x.transaction_receipt = None;
        print_debug(x);

        let mut x = proto::EthGetTransactionReceiptResponse::default();
        x.transaction_receipt = Some(EthTransactionReceipt::default());
        print_debug(x);
    }

    #[test]
    fn json_outputs_chain_id() {
        let result = add(2, 2);
        assert_eq!(result, 4);

        let mut x = proto::EthChainIdResponse::default();
        x.id = 100.to_string();
        print_debug(x);
    }
}
