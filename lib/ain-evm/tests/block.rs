#![cfg(test_off)]

use std::str::FromStr;

use ain_evm::transaction::SignedTx;
use primitive_types::{H160, H256, U256};

use ain_evm::evm::EVMServices;

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
    handler.evm.tx_queues.add_signed_tx(context, tx1);

    let old_state = handler.evm.state.read().unwrap();
    let _ = handler.finalize_block(context, false, 0, None).unwrap();

    let new_state = handler.evm.state.read().unwrap();
    assert_eq!(*new_state, *old_state);
}

#[test]
fn test_finalize_block_and_update_state() {
    let handler = Handlers::new();
    let context = handler.evm.get_context();
    handler.evm.add_balance(
        context,
        "0xebf9844ba89c4975bbe4e621dbaf085e6357df3f"
            .parse()
            .unwrap(),
        U256::from_str_radix("100000000000000000000", 10).unwrap(),
    );

    let tx1: SignedTx = "f86b02830186a0830186a094a8f7c4c78c36e54c3950ad58dad24ca5e0191b2989056bc75e2d631000008025a0b0842b0c78dd7fc33584ec9a81ab5104fe70169878de188ba6c11fe7605e298aa0735dc483f625f17d68d1e1fae779b7160612628e6dde9eecf087892fe60bba4e".try_into().unwrap();
    handler.evm.tx_queues.add_signed_tx(context, tx1.clone());

    handler.evm.add_balance(
        context,
        "0x47b16da33f4e7e4a4ed9e52cc561b9ffcb3daf56"
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

    let FinalizedBlockInfo { block, failed_txs } =
        handler.finalize_block(context, true, 0, None).unwrap();

    assert_eq!(
        block.transactions,
        vec![tx1, tx2, tx3.clone()]
            .into_iter()
            .map(|t| t.transaction)
            .collect::<Vec<_>>()
    );
    assert_eq!(
        failed_txs,
        vec![tx3]
            .into_iter()
            .map(|t| hex::encode(ethereum::EnvelopedEncodable::encode(&t.transaction)))
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
                &"0x47b16da33f4e7e4a4ed9e52cc561b9ffcb3daf56"
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
                &"0xebf9844ba89c4975bbe4e621dbaf085e6357df3f"
                    .parse()
                    .unwrap()
            )
            .unwrap()
            .balance,
        U256::from_str_radix("0", 10).unwrap()
    );
}

#[test]
fn test_deploy_and_call_smart_contract() {
    let smart_contract_address: H160 = "69762793de93f55ab919c5efdaafb63d413dcbb5".parse().unwrap();

    let handler = Handlers::new();
    let context = handler.evm.get_context();
    handler.evm.add_balance(
        context,
        "0x4a1080c5533cb89edc4b65013f08f78868e382de"
            .parse()
            .unwrap(),
        U256::from_str_radix("100000000000000000000", 10).unwrap(),
    );

    // Create simple storage smart contract
    let create_smart_contract_tx: SignedTx = "f9044e808504a817c800832dc6c08080b903fb608060405234801561001057600080fd5b506103db806100206000396000f3fe608060405234801561001057600080fd5b50600436106100365760003560e01c8063131a06801461003b5780632e64cec114610050575b600080fd5b61004e61004936600461015d565b61006e565b005b6100586100b5565b604051610065919061020e565b60405180910390f35b600061007a82826102e5565b507ffe3101cc3119e1fe29a9c3464a3ff7e98501e65122abab6937026311367dc516816040516100aa919061020e565b60405180910390a150565b6060600080546100c49061025c565b80601f01602080910402602001604051908101604052809291908181526020018280546100f09061025c565b801561013d5780601f106101125761010080835404028352916020019161013d565b820191906000526020600020905b81548152906001019060200180831161012057829003601f168201915b5050505050905090565b634e487b7160e01b600052604160045260246000fd5b60006020828403121561016f57600080fd5b813567ffffffffffffffff8082111561018757600080fd5b818401915084601f83011261019b57600080fd5b8135818111156101ad576101ad610147565b604051601f8201601f19908116603f011681019083821181831017156101d5576101d5610147565b816040528281528760208487010111156101ee57600080fd5b826020860160208301376000928101602001929092525095945050505050565b600060208083528351808285015260005b8181101561023b5785810183015185820160400152820161021f565b506000604082860101526040601f19601f8301168501019250505092915050565b600181811c9082168061027057607f821691505b60208210810361029057634e487b7160e01b600052602260045260246000fd5b50919050565b601f8211156102e057600081815260208120601f850160051c810160208610156102bd5750805b601f850160051c820191505b818110156102dc578281556001016102c9565b5050505b505050565b815167ffffffffffffffff8111156102ff576102ff610147565b6103138161030d845461025c565b84610296565b602080601f83116001811461034857600084156103305750858301515b600019600386901b1c1916600185901b1785556102dc565b600085815260208120601f198616915b8281101561037757888601518255948401946001909101908401610358565b50858210156103955787850151600019600388901b60f8161c191681555b5050505050600190811b0190555056fea2646970667358221220f5c9bb4feb3fa563cfe06a38d411044d98edf92f98726288036607edd71587b564736f6c634300081100332aa06aa3b6274fbd96df7215c2b791c766e21a65d97467ddbd90c0d869ba51d04387a05512f44e35c5ab3c1716373877503d03a5f9ebdf5b7645e8fb30b308a6f046f8".try_into().unwrap();
    handler
        .evm
        .tx_queues
        .add_signed_tx(context, create_smart_contract_tx);

    handler.finalize_block(context, true, 0, None).unwrap();

    // Fund caller address
    handler.evm.add_balance(
        context,
        "0xb069baef499f992ff243300f78cf9ca1406a122e"
            .parse()
            .unwrap(),
        U256::from_str_radix("100000000000000000000000000", 10).unwrap(),
    );
    let call_smart_contract_tx: SignedTx = "f8ca018504a817c8008302c1789469762793de93f55ab919c5efdaafb63d413dcbb580b864131a06800000000000000000000000000000000000000000000000000000000000000020000000000000000000000000000000000000000000000000000000000000000d48656c6c6f2c20576f726c64210000000000000000000000000000000000000029a041fc9c0581885d77263dcba0603d8c6c164a9acfe803ad11188069eafa22169ca0018c1ba512639bd8ce32e76bcc2ea0759073a3f908014e47544d6c6674388b37".try_into().unwrap();

    // Each block requires a new context
    let context = handler.evm.get_context();
    handler
        .evm
        .tx_queues
        .add_signed_tx(context, call_smart_contract_tx);

    handler.finalize_block(context, true, 0, None).unwrap();

    let smart_contract_storage = handler.evm.get_storage(smart_contract_address);
    assert_eq!(
        smart_contract_storage.get(&H256::zero()),
        Some(
            &H256::from_str("0x48656c6c6f2c20576f726c64210000000000000000000000000000000000001a")
                .unwrap()
        )
    )
}
