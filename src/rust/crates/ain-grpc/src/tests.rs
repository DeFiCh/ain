#![cfg(test)]

use std::str::FromStr;
use std::sync::Arc;

use primitive_types::{H160, U256};

use ain_evm_state::handler::Handlers;

use crate::{
    codegen::{
        rpc::EthService,
        types::{EthCallInput, EthGetBalanceInput, EthTransactionInfo},
    },
    rpc::EthServiceApi,
};

const ALICE: &str = "0x0000000000000000000000000000000000000000";
const BOB: &str = "0x0000000000000000000000000000000000000001";

#[test]
fn should_call() {
    let handler = Arc::new(Handlers::new());
    let tx_info = EthTransactionInfo {
        from: ALICE.to_string(),
        to: BOB.to_string(),
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

    let _ = handler.evm.finalize_block(ctx, true);

    let res2 = EthService::Eth_GetBalance(handler, input.into());
    assert_eq!(res2.unwrap().balance, "1337");
}
