use ain_evm::transaction::SignedTx;
use primitive_types::U256;

use ain_evm_state::handler::Handlers;

#[test]
fn test_finalize_block_and_do_not_update_state() {
    let handler = Handlers::new();
    let context = handler.evm.get_context();
    handler.evm.add_balance(
        context,
        "0x4a1080c5533cb89edc4b65013f08f78868e382de"
            .parse()
            .unwrap(),
        U256::from_str_radix("100000000000000000000", 10).unwrap(),
    );

    let tx1: SignedTx = "f86b02830186a0830186a094a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a0b0842b0c78dd7fc33584ec9a81ab5104fe70169878de188ba6c11fe7605e298aa0735dc483f625f17d68d1e1fae779b7160612628e6dde9eecf087892fe60bba4e".try_into().unwrap();
    println!("tx1 : {:#?}", tx1);
    handler.evm.tx_queues.add_signed_tx(context, tx1.clone());

    let old_state = handler.evm.state.read().unwrap();
    let _ = handler.evm.finalize_block(context, false).unwrap();

    let new_state = handler.evm.state.read().unwrap();
    assert_eq!(*new_state, *old_state);
}

#[test]
fn test_finalize_block_and_update_state() {
    let handler = Handlers::new();
    let context = handler.evm.get_context();
    handler.evm.add_balance(
        context,
        "0x6745f998a96050bb9b0449e6bd4358138a519679"
            .parse()
            .unwrap(),
        U256::from_str_radix("100000000000000000000", 10).unwrap(),
    );

    let tx1: SignedTx = "f86b02830186a0830186a094a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a0b0842b0c78dd7fc33584ec9a81ab5104fe70169878de188ba6c11fe7605e298aa0735dc483f625f17d68d1e1fae779b7160612628e6dde9eecf087892fe60bba4e".try_into().unwrap();
    handler.evm.tx_queues.add_signed_tx(context, tx1.clone());

    handler.evm.add_balance(
        context,
        "0xc0cd829081485e70348975d325fe5275140277bd"
            .parse()
            .unwrap(),
        U256::from_str_radix("100000000000000000000", 10).unwrap(),
    );
    let tx2: SignedTx = "f86b02830186a0830186a094a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a01465e2d999c34b22bf4b8b5c9439918e46341f4f0da1b00a6b0479c541161d4aa074abe79c51bf57086e1e84b57ee483cbb2ecf30e8222bc0472436fabfc57dda8".try_into().unwrap();
    handler.evm.tx_queues.add_signed_tx(context, tx2.clone());

    let tx3: SignedTx = "f86b02830186a0830186a094a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a070b21a24cec13c0569099ee2f8221268103fd609646b73f7c9e85efeb7af5c8ea03d5de75bc12ce28a80f7c0401df6021cc82a334cb1c802c8b9d46223c5c8eb40".try_into().unwrap();
    handler.evm.tx_queues.add_signed_tx(context, tx3.clone());

    assert_eq!(handler.evm.tx_queues.len(context), 3);
    assert_eq!(handler.evm.tx_queues.len(handler.evm.get_context()), 0);

    let (block, failed_txs) = handler.evm.finalize_block(context, true).unwrap();
    assert_eq!(
        block.transactions,
        vec![tx1, tx2]
            .into_iter()
            .map(|t| t.transaction)
            .collect::<Vec<_>>()
    );
    assert_eq!(
        failed_txs,
        vec![tx3]
            .into_iter()
            .map(|t| t.transaction)
            .collect::<Vec<_>>()
    );

    let state = handler.evm.state.read().unwrap();
    assert_eq!(
        state
            .get(
                &"0xa8f7c4c78c36e54c3950ad58dad24ca5e0191b29"
                    .parse()
                    .unwrap()
            )
            .unwrap()
            .balance,
        U256::from_str_radix("200000000000000000000", 10).unwrap()
    );
    assert_eq!(
        state
            .get(
                &"0x6745f998a96050bb9b0449e6bd4358138a519679"
                    .parse()
                    .unwrap()
            )
            .unwrap()
            .balance,
        U256::from_str_radix("0", 10).unwrap()
    );
    assert_eq!(
        state
            .get(
                &"0xc0cd829081485e70348975d325fe5275140277bd"
                    .parse()
                    .unwrap()
            )
            .unwrap()
            .balance,
        U256::from_str_radix("0", 10).unwrap()
    );
}
