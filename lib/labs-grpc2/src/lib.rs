mod api;
mod proto;

#[cfg(test)]
mod tests {
    use serde::Serialize;

    use super::*;
    use crate::proto::eth::*;

    fn print_debug<T: std::fmt::Debug + Serialize>(val: T) {
        println!("{:?}\n{}", val, serde_json::to_string_pretty(&val).unwrap());
    }

    #[test]
    fn json_outputs_basic() {
        let mut x = EthCallResponse::default();
        x.data = "hello".to_string();
        print_debug(x);
    }

    #[test]
    fn json_outputs_flattened_enums() {
        let mut x: EthSyncingResponse = EthSyncingResponse::default();
        x.value = Some(eth_syncing_response::Value::Status(true));
        print_debug(x);

        let mut x = EthSyncingResponse::default();
        x.value = Some(eth_syncing_response::Value::SyncInfo(
            EthSyncingInfo::default(),
        ));
        print_debug(x);
    }

    #[test]
    fn json_outputs_transaction_receipts() {
        let mut x = EthGetTransactionReceiptResponse::default();
        x.transaction_receipt = None;
        print_debug(x);

        let mut x = EthGetTransactionReceiptResponse::default();
        x.transaction_receipt = Some(EthTransactionReceipt::default());
        print_debug(x);
    }

    #[test]
    fn json_outputs_chain_id() {
        let mut x = EthChainIdResponse::default();
        x.id = 100.to_string();
        print_debug(x);
    }
}
