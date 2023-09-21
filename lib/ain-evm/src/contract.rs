use ain_contracts::{
    get_dst20_contract, get_dst20_deploy_input, get_intrinsic_contract, get_reserved_contract,
    get_transferdomain_contract, Contract, FixedContract,
};
use anyhow::format_err;
use ethbloom::Bloom;
use ethereum::{
    EIP1559ReceiptData, LegacyTransaction, ReceiptV3, TransactionAction, TransactionSignature,
    TransactionV2,
};
use ethereum_types::{H160, H256, U256};
use log::trace;
use sha3::{Digest, Keccak256};

use crate::{
    backend::EVMBackend,
    bytes::Bytes,
    executor::AinExecutor,
    transaction::{
        system::{DeployContractData, SystemTx, TransferDirection},
        SignedTx, LOWER_H256,
    },
    txqueue::{QueueTx, QueueTxItem},
    Result,
};

pub fn u256_to_h256(input: U256) -> H256 {
    let mut bytes = [0_u8; 32];
    input.to_big_endian(&mut bytes);

    H256::from(bytes)
}

pub fn get_abi_encoded_string(input: &str) -> H256 {
    let length = input.len();

    let mut storage_value = H256::default();
    storage_value.0[31] = (length * 2) as u8;
    storage_value.0[..length].copy_from_slice(input.as_bytes());

    storage_value
}

pub fn get_address_storage_index(slot: H256, address: H160) -> H256 {
    // padded key
    let key = H256::from(address);

    // keccak256(padded key + padded slot)
    let mut hasher = Keccak256::new();
    hasher.update(key.as_fixed_bytes());
    hasher.update(slot.as_fixed_bytes());
    let hash_result = hasher.finalize();

    let mut index_bytes = [0u8; 32];
    index_bytes.copy_from_slice(&hash_result);

    H256::from(index_bytes)
}

pub struct DeployContractInfo {
    pub address: H160,
    pub storage: Vec<(H256, H256)>,
    pub bytecode: Bytes,
}

pub struct DST20BridgeInfo {
    pub address: H160,
    pub storage: Vec<(H256, H256)>,
}

/// Returns address, bytecode and storage with incremented count for the counter contract
pub fn intrinsics_contract(
    backend: &EVMBackend,
    dvm_block_number: u64,
    evm_block_number: U256,
) -> Result<DeployContractInfo> {
    let FixedContract {
        contract,
        fixed_address,
        ..
    } = get_intrinsic_contract();
    let count =
        backend.get_contract_storage(fixed_address, u256_to_h256(U256::one()).as_bytes())?;

    trace!("Count: {:#x}", count + U256::one());

    Ok(DeployContractInfo {
        address: fixed_address,
        bytecode: Bytes::from(contract.runtime_bytecode),
        storage: vec![
            (H256::from_low_u64_be(0), u256_to_h256(U256::one())),
            (H256::from_low_u64_be(1), u256_to_h256(evm_block_number)),
            (
                H256::from_low_u64_be(2),
                u256_to_h256(U256::from(dvm_block_number)),
            ),
        ],
    })
}

/// Returns transfer domain address, bytecode and null storage
pub fn transfer_domain_contract() -> DeployContractInfo {
    let FixedContract {
        contract,
        fixed_address,
    } = get_transferdomain_contract();

    DeployContractInfo {
        address: fixed_address,
        bytecode: Bytes::from(contract.runtime_bytecode),
        storage: Vec::new(),
    }
}

pub fn dst20_contract(
    backend: &EVMBackend,
    address: H160,
    name: &str,
    symbol: &str,
) -> Result<DeployContractInfo> {
    match backend.get_account(&address) {
        None => {}
        Some(account) => {
            let Contract { codehash, .. } = get_reserved_contract();
            if account.code_hash != codehash {
                return Err(format_err!("Token {symbol} address is already in use").into());
            }
        }
    }

    let Contract {
        runtime_bytecode, ..
    } = get_dst20_contract();
    let storage = vec![
        (H256::from_low_u64_be(3), get_abi_encoded_string(name)),
        (H256::from_low_u64_be(4), get_abi_encoded_string(symbol)),
    ];

    Ok(DeployContractInfo {
        address,
        bytecode: Bytes::from(runtime_bytecode),
        storage,
    })
}

pub fn bridge_dst20(
    backend: &EVMBackend,
    contract: H160,
    from: H160,
    amount: U256,
    direction: TransferDirection,
) -> Result<DST20BridgeInfo> {
    // check if code of address matches DST20 bytecode
    let account = backend
        .get_account(&contract)
        .ok_or_else(|| format_err!("DST20 token address is not a contract"))?;

    let FixedContract { fixed_address, .. } = get_transferdomain_contract();

    let Contract { codehash, .. } = get_dst20_contract();
    if account.code_hash != codehash {
        return Err(format_err!("DST20 token code is not valid").into());
    }

    // balance has slot 0
    let balance_storage_index = get_address_storage_index(H256::zero(), from);
    let balance = backend.get_contract_storage(contract, balance_storage_index.as_bytes())?;

    // allowance has slot 1
    let allowance_storage_index = get_address_storage_index(H256::from_low_u64_be(1), from);
    let address_allowance_storage_index =
        get_address_storage_index(allowance_storage_index, fixed_address);

    let total_supply_index = H256::from_low_u64_be(2);
    let total_supply = backend.get_contract_storage(contract, total_supply_index.as_bytes())?;

    let (new_balance, new_total_supply) = if direction == TransferDirection::EvmOut {
        (
            balance.checked_sub(amount),
            total_supply.checked_sub(amount),
        )
    } else {
        (
            balance.checked_add(amount),
            total_supply.checked_add(amount),
        )
    };

    let new_balance = new_balance.ok_or_else(|| format_err!("Balance overflow/underflow"))?;
    let new_total_supply =
        new_total_supply.ok_or_else(|| format_err!("Total supply overflow/underflow"))?;

    Ok(DST20BridgeInfo {
        address: contract,
        storage: vec![
            (balance_storage_index, u256_to_h256(new_balance)),
            (total_supply_index, u256_to_h256(new_total_supply)),
            (address_allowance_storage_index, u256_to_h256(amount)),
        ],
    })
}

pub fn bridge_dfi(
    backend: &EVMBackend,
    amount: U256,
    direction: TransferDirection,
) -> Result<Vec<(H256, H256)>> {
    let FixedContract { fixed_address, .. } = get_transferdomain_contract();

    let total_supply_index = H256::from_low_u64_be(1);
    let total_supply =
        backend.get_contract_storage(fixed_address, total_supply_index.as_bytes())?;

    let new_total_supply = if direction == TransferDirection::EvmOut {
        total_supply.checked_sub(amount)
    } else {
        total_supply.checked_add(amount)
    };

    let new_total_supply =
        new_total_supply.ok_or_else(|| format_err!("Total supply overflow/underflow"))?;

    Ok(vec![(total_supply_index, u256_to_h256(new_total_supply))])
}

pub fn reserve_dst20_namespace(executor: &mut AinExecutor) -> Result<()> {
    let Contract {
        runtime_bytecode, ..
    } = get_reserved_contract();
    let addresses = (0..1024)
        .map(|token_id| ain_contracts::dst20_address_from_token_id(token_id).unwrap())
        .collect::<Vec<H160>>();

    for address in addresses {
        trace!(
            "[reserve_dst20_namespace] Deploying address to {:#?}",
            address
        );
        executor.deploy_contract(address, runtime_bytecode.clone().into(), Vec::new())?;
    }

    Ok(())
}

pub fn reserve_intrinsics_namespace(executor: &mut AinExecutor) -> Result<()> {
    let Contract {
        runtime_bytecode, ..
    } = get_reserved_contract();
    let addresses = (0..128)
        .map(|token_id| ain_contracts::intrinsics_address_from_id(token_id).unwrap())
        .collect::<Vec<H160>>();

    for address in addresses {
        trace!(
            "[reserve_intrinsics_namespace] Deploying address to {:#?}",
            address
        );
        executor.deploy_contract(address, runtime_bytecode.clone().into(), Vec::new())?;
    }

    Ok(())
}

pub fn dst20_deploy_contract_tx(
    token_id: u64,
    base_fee: &U256,
    name: &str,
    symbol: &str,
) -> Result<(Box<SignedTx>, ReceiptV3)> {
    let tx = TransactionV2::Legacy(LegacyTransaction {
        nonce: U256::from(token_id),
        gas_price: *base_fee,
        gas_limit: U256::from(u64::MAX),
        action: TransactionAction::Create,
        value: U256::zero(),
        input: get_dst20_deploy_input(get_dst20_contract().init_bytecode, name, symbol)
            .map_err(|e| format_err!(e))?,
        signature: TransactionSignature::new(27, LOWER_H256, LOWER_H256)
            .ok_or(format_err!("Invalid transaction signature format"))?,
    })
    .try_into()?;

    let receipt = get_default_successful_receipt();

    Ok((Box::new(tx), receipt))
}

pub fn transfer_domain_deploy_contract_tx(base_fee: &U256) -> Result<(SignedTx, ReceiptV3)> {
    let tx = TransactionV2::Legacy(LegacyTransaction {
        nonce: U256::zero(),
        gas_price: *base_fee,
        gas_limit: U256::from(u64::MAX),
        action: TransactionAction::Create,
        value: U256::zero(),
        input: get_transferdomain_contract().contract.init_bytecode,
        signature: TransactionSignature::new(27, LOWER_H256, LOWER_H256)
            .ok_or(format_err!("Invalid transaction signature format"))?,
    })
    .try_into()?;

    let receipt = get_default_successful_receipt();

    Ok((tx, receipt))
}

fn get_default_successful_receipt() -> ReceiptV3 {
    ReceiptV3::Legacy(EIP1559ReceiptData {
        status_code: 1u8,
        used_gas: U256::zero(),
        logs_bloom: Bloom::default(),
        logs: Vec::new(),
    })
}

pub fn get_dst20_migration_txs(mnview_ptr: usize) -> Result<Vec<QueueTxItem>> {
    let mut txs = Vec::new();
    for token in ain_cpp_imports::get_dst20_tokens(mnview_ptr) {
        let address = ain_contracts::dst20_address_from_token_id(token.id)?;
        trace!(
            "[get_dst20_migration_txs] Deploying to address {:#?}",
            address
        );

        let tx = QueueTx::SystemTx(SystemTx::DeployContract(DeployContractData {
            name: token.name,
            symbol: token.symbol,
            token_id: token.id,
            address,
        }));
        txs.push(QueueTxItem {
            tx,
            tx_hash: Default::default(),
            gas_used: U256::zero(),
            state_root: Default::default(),
        });
    }
    Ok(txs)
}
