#![cfg(test)]

use ethereum::{BlockV2, PartialHeader};
use std::str::FromStr;
use std::sync::Arc;

use primitive_types::{H160, U256};

use ain_evm::handler::Handlers;

use crate::{
    codegen::{
        rpc::EthService,
        types::{EthCallInput, EthGetBalanceInput, EthGetBlockByHashInput, EthTransactionInfo},
    },
    rpc::EthServiceApi,
};

const ALICE: &str = "0x0000000000000000000000000000000000000000";
const BOB: &str = "0x0000000000000000000000000000000000000001";

#[test]
fn should_call() {
    let handler = Arc::new(Handlers::new());
    let tx_info = EthTransactionInfo {
        from: Some(ALICE.to_string()),
        to: Some(BOB.to_string()),
        gas: None,
        price: None,
        value: None,
        data: Some("0x2394872".to_string()),
        nonce: None,
    };
    let input = EthCallInput {
        transaction_info: Some(tx_info),
        block_number: "latest".to_string(),
    };
    let res = EthService::Eth_Call(handler, input.into());
    assert!(res.is_ok());
}

#[test]
fn should_get_balance() {
    let handler = Arc::new(Handlers::new());
    let input = EthGetBalanceInput {
        address: ALICE.to_string(),
        block_number: "latest".to_string(),
    };

    let res = EthService::Eth_GetBalance(handler.clone(), input.clone().into());
    assert_eq!(res.unwrap().balance, "0");

    let ctx = handler.evm.get_context();

    handler
        .evm
        .add_balance(ctx, H160::from_str(ALICE).unwrap(), U256::from(1337));

    let _ = handler.finalize_block(ctx, true, None);

    let res2 = EthService::Eth_GetBalance(handler, input.into());
    assert_eq!(res2.unwrap().balance, "1337");
}

#[test]
fn should_get_block_by_hash() {
    let handler = Arc::new(Handlers::new());
    let block = BlockV2::new(
        PartialHeader {
            parent_hash: Default::default(),
            beneficiary: Default::default(),
            state_root: Default::default(),
            receipts_root: Default::default(),
            logs_bloom: Default::default(),
            difficulty: Default::default(),
            number: Default::default(),
            gas_limit: Default::default(),
            gas_used: Default::default(),
            timestamp: 0,
            extra_data: vec![],
            mix_hash: Default::default(),
            nonce: Default::default(),
        },
        Vec::new(),
        Vec::new(),
    );
    handler.block.connect_block(block.clone());

    let binding = handler.block.block_map.read().unwrap();
    let _bno = binding.get(&block.header.hash()).unwrap();

    let input = EthGetBlockByHashInput {
        hash: format!("{:x}", block.header.hash()),
        full_transaction: false,
    };
    let res = EthService::Eth_GetBlockByHash(handler.clone(), input.clone().into());
    assert_eq!(
        format!("{}", res.unwrap().hash),
        format!("0x{:x}", block.header.hash())
    );
}
