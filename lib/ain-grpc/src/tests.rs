#![cfg(test_off)]

use ethereum::{BlockV2, PartialHeader};
use std::str::FromStr;
use std::sync::Arc;

use primitive_types::{H160, U256};

use crate::codegen::types::*;
use ain_evm::handler::Handlers;
use ain_evm::transaction::SignedTx;

const ALICE: &str = "0x0000000000000000000000000000000000000000";
const BOB: &str = "0x0000000000000000000000000000000000000001";

#[test]
fn should_call() {
    let handler = Arc::new(Handlers::new());
    let tx_info = EthTransactionInfo {
        from: Some(ALICE.to_string()),
        to: Some(BOB.to_string()),
        gas: Default::default(),
        price: Default::default(),
        value: Default::default(),
        data: Some("0x2394872".to_string()),
        nonce: Default::default(),
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
    handler.storage.put_block(block.clone());

    let block = handler
        .block
        .get_block_by_hash(block.header.hash())
        .unwrap();

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

#[test]
fn should_get_transaction_by_hash() {
    let handler = Arc::new(Handlers::new());
    let signed_tx: SignedTx = "f86b02830186a0830186a094a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a0b0842b0c78dd7fc33584ec9a81ab5104fe70169878de188ba6c11fe7605e298aa0735dc483f625f17d68d1e1fae779b7160612628e6dde9eecf087892fe60bba4e".try_into().unwrap();
    let tx_hashes = vec![signed_tx.clone().transaction];
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
        tx_hashes,
        Vec::new(),
    );

    handler.block.connect_block(block.clone());
    handler.storage.put_block(block.clone());

    let input = EthGetTransactionByHashInput {
        hash: format!("{:x}", signed_tx.transaction.hash()),
    };

    let res = EthService::Eth_GetTransactionByHash(handler.clone(), input.clone().into()).unwrap();

    assert_eq!(res.from.parse::<H160>().unwrap(), signed_tx.sender);
    assert_eq!(res.to.parse::<H160>().ok(), signed_tx.to());
    assert_eq!(res.gas, signed_tx.gas_limit().as_u64());
    assert_eq!(
        U256::from_str_radix(&res.price, 10).unwrap(),
        signed_tx.gas_price()
    );
    assert_eq!(
        U256::from_str_radix(&res.value, 10).unwrap(),
        signed_tx.value()
    );
    assert_eq!(res.data, hex::encode(signed_tx.data()));
    assert_eq!(
        U256::from_str_radix(&res.nonce, 10).unwrap(),
        signed_tx.nonce()
    );
}

#[test]
fn should_get_transaction_by_block_hash_and_index() {
    let handler = Arc::new(Handlers::new());
    let signed_tx: SignedTx = "f86b02830186a0830186a094a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a0b0842b0c78dd7fc33584ec9a81ab5104fe70169878de188ba6c11fe7605e298aa0735dc483f625f17d68d1e1fae779b7160612628e6dde9eecf087892fe60bba4e".try_into().unwrap();
    let tx_hashes = vec![signed_tx.clone().transaction];
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
        tx_hashes,
        Vec::new(),
    );

    handler.block.connect_block(block.clone());
    handler.storage.put_block(block.clone());

    let input = EthGetTransactionByBlockHashAndIndexInput {
        block_hash: format!("{:x}", block.header.hash()),
        index: String::from("0"),
    };

    let res =
        EthService::Eth_GetTransactionByBlockHashAndIndex(handler.clone(), input.clone().into())
            .unwrap();

    assert_eq!(res.from.parse::<H160>().unwrap(), signed_tx.sender);
    assert_eq!(res.to.parse::<H160>().ok(), signed_tx.to());
    assert_eq!(res.gas, signed_tx.gas_limit().as_u64());
    assert_eq!(
        U256::from_str_radix(&res.price, 10).unwrap(),
        signed_tx.gas_price()
    );
    assert_eq!(
        U256::from_str_radix(&res.value, 10).unwrap(),
        signed_tx.value()
    );
    assert_eq!(res.data, hex::encode(signed_tx.data()));
    assert_eq!(
        U256::from_str_radix(&res.nonce, 10).unwrap(),
        signed_tx.nonce()
    );
}

#[test]
fn should_get_transaction_by_block_number_and_index() {
    let handler = Arc::new(Handlers::new());
    let signed_tx: SignedTx = "f86b02830186a0830186a094a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a0b0842b0c78dd7fc33584ec9a81ab5104fe70169878de188ba6c11fe7605e298aa0735dc483f625f17d68d1e1fae779b7160612628e6dde9eecf087892fe60bba4e".try_into().unwrap();
    let tx_hashes = vec![signed_tx.clone().transaction];
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
        tx_hashes,
        Vec::new(),
    );

    handler.block.connect_block(block.clone());
    handler.storage.put_block(block.clone());

    let input = EthGetTransactionByBlockNumberAndIndexInput {
        block_number: format!("{:x}", block.header.number),
        index: String::from("0"),
    };

    let res =
        EthService::Eth_GetTransactionByBlockNumberAndIndex(handler.clone(), input.clone().into())
            .unwrap();

    assert_eq!(res.from.parse::<H160>().unwrap(), signed_tx.sender);
    assert_eq!(res.to.parse::<H160>().ok(), signed_tx.to());
    assert_eq!(res.gas, signed_tx.gas_limit().as_u64());
    assert_eq!(
        U256::from_str_radix(&res.price, 10).unwrap(),
        signed_tx.gas_price()
    );
    assert_eq!(
        U256::from_str_radix(&res.value, 10).unwrap(),
        signed_tx.value()
    );
    assert_eq!(res.data, hex::encode(signed_tx.data()));
    assert_eq!(
        U256::from_str_radix(&res.nonce, 10).unwrap(),
        signed_tx.nonce()
    );
}
