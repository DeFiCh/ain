use ain_contracts::{
    get_dfi_reserved_contract, get_dst20_contract, get_dst20_v1_contract, get_dst20_v2_contract,
    get_transfer_domain_contract, Contract, FixedContract, IMPLEMENTATION_SLOT,
};
use anyhow::format_err;
use ethereum::{
    LegacyTransaction, ReceiptV3, TransactionAction, TransactionSignature, TransactionV2,
};
use ethereum_types::{H160, H256, U256};
use log::trace;

use crate::{
    backend::EVMBackend,
    bytes::Bytes,
    executor::AinExecutor,
    transaction::{
        system::{DeployContractData, ExecuteTx, SystemTx, TransferDirection},
        SignedTx, LOWER_H256,
    },
    Result,
};

use super::{
    get_abi_encoded_string, get_address_storage_index, get_default_successful_receipt,
    h160_to_h256, u256_to_h256, DeployContractInfo,
};

pub struct DST20BridgeInfo {
    pub address: H160,
    pub storage: Vec<(H256, H256)>,
}

pub fn dst20_deploy_info(
    backend: &EVMBackend,
    dvm_block: u64,
    address: H160,
    name: &str,
    symbol: &str,
) -> Result<DeployContractInfo> {
    // TODO: Move checks outside of the deploy_info
    match backend.get_account(&address) {
        None => {}
        Some(account) => {
            let Contract { codehash, .. } = get_dfi_reserved_contract();
            if account.code_hash != codehash {
                return Err(format_err!("Token {symbol} address is already in use").into());
            }
        }
    }

    let Contract {
        runtime_bytecode, ..
    } = get_dst20_contract();
    let storage = dst20_name_info(dvm_block, name, symbol);

    Ok(DeployContractInfo {
        address,
        bytecode: Bytes::from(runtime_bytecode),
        storage,
    })
}

pub fn dst20_v1_deploy_info() -> DeployContractInfo {
    let FixedContract {
        contract,
        fixed_address,
    } = get_dst20_v1_contract();

    DeployContractInfo {
        address: fixed_address,
        bytecode: Bytes::from(contract.runtime_bytecode),
        storage: Vec::new(),
    }
}

pub fn dst20_v2_deploy_info() -> DeployContractInfo {
    let FixedContract {
        contract,
        fixed_address,
    } = get_dst20_v2_contract();

    DeployContractInfo {
        address: fixed_address,
        bytecode: Bytes::from(contract.runtime_bytecode),
        storage: Vec::new(),
    }
}

pub fn bridge_dst20_in(
    backend: &EVMBackend,
    contract: H160,
    amount: U256,
) -> Result<DST20BridgeInfo> {
    // check if code of address matches DST20 bytecode
    let account = backend
        .get_account(&contract)
        .ok_or_else(|| format_err!("DST20 token address is not a contract"))?;

    let FixedContract { fixed_address, .. } = get_transfer_domain_contract();

    let Contract { codehash, .. } = get_dst20_contract();
    if account.code_hash != codehash {
        return Err(format_err!("DST20 token code is not valid").into());
    }

    // balance has slot 0
    let contract_balance_storage_index = get_address_storage_index(H256::zero(), fixed_address);

    let total_supply_index = H256::from_low_u64_be(2);
    let total_supply = backend.get_contract_storage(contract, total_supply_index)?;

    let new_total_supply = total_supply
        .checked_add(amount)
        .ok_or_else(|| format_err!("Total supply overflow/underflow"))?;

    Ok(DST20BridgeInfo {
        address: contract,
        storage: vec![
            (contract_balance_storage_index, u256_to_h256(amount)),
            (total_supply_index, u256_to_h256(new_total_supply)),
        ],
    })
}

pub fn bridge_dst20_out(
    backend: &EVMBackend,
    contract: H160,
    amount: U256,
) -> Result<DST20BridgeInfo> {
    // check if code of address matches DST20 bytecode
    let account = backend
        .get_account(&contract)
        .ok_or_else(|| format_err!("DST20 token address is not a contract"))?;

    let FixedContract { fixed_address, .. } = get_transfer_domain_contract();

    let Contract { codehash, .. } = get_dst20_contract();
    if account.code_hash != codehash {
        return Err(format_err!("DST20 token code is not valid").into());
    }

    let contract_balance_storage_index = get_address_storage_index(H256::zero(), fixed_address);

    let total_supply_index = H256::from_low_u64_be(2);
    let total_supply = backend.get_contract_storage(contract, total_supply_index)?;

    let new_total_supply = total_supply
        .checked_sub(amount)
        .ok_or_else(|| format_err!("Total supply overflow/underflow"))?;

    Ok(DST20BridgeInfo {
        address: contract,
        storage: vec![
            (contract_balance_storage_index, u256_to_h256(U256::zero())), // Reset contract balance to 0
            (total_supply_index, u256_to_h256(new_total_supply)),
        ],
    })
}

pub fn dst20_allowance(
    direction: TransferDirection,
    from: H160,
    amount: U256,
) -> Vec<(H256, H256)> {
    let FixedContract { fixed_address, .. } = get_transfer_domain_contract();

    // allowance has slot 1
    let allowance_storage_index = get_address_storage_index(
        H256::from_low_u64_be(1),
        if direction == TransferDirection::EvmIn {
            fixed_address
        } else {
            from
        },
    );
    let address_allowance_storage_index =
        get_address_storage_index(allowance_storage_index, fixed_address);

    vec![(address_allowance_storage_index, u256_to_h256(amount))]
}

pub fn reserve_dst20_namespace(executor: &mut AinExecutor) -> Result<()> {
    let Contract {
        runtime_bytecode, ..
    } = get_dfi_reserved_contract();
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

pub fn dst20_deploy_contract_tx(
    token_id: u64,
    base_fee: &U256,
) -> Result<(Box<SignedTx>, ReceiptV3)> {
    let tx = TransactionV2::Legacy(LegacyTransaction {
        nonce: U256::from(token_id),
        gas_price: *base_fee,
        gas_limit: U256::from(u64::MAX),
        action: TransactionAction::Create,
        value: U256::zero(),
        input: get_transfer_domain_contract().contract.init_bytecode,
        signature: TransactionSignature::new(27, LOWER_H256, LOWER_H256)
            .ok_or(format_err!("Invalid transaction signature format"))?,
    })
    .try_into()?;

    let receipt = get_default_successful_receipt();

    Ok((Box::new(tx), receipt))
}

pub fn get_dst20_migration_txs(mnview_ptr: usize) -> Result<Vec<ExecuteTx>> {
    let mut tokens = vec![];
    let mut txs = Vec::new();
    if !ain_cpp_imports::get_dst20_tokens(mnview_ptr, &mut tokens) {
        return Err(format_err!("DST20 token migration failed, invalid token name.").into());
    }

    for token in tokens {
        let address = ain_contracts::dst20_address_from_token_id(token.id)?;
        trace!(
            "[get_dst20_migration_txs] Deploying to address {:#?}",
            address
        );

        let tx = ExecuteTx::SystemTx(SystemTx::DeployContract(DeployContractData {
            name: token.name,
            symbol: token.symbol,
            token_id: token.id,
            address,
        }));
        txs.push(tx);
    }
    Ok(txs)
}

pub fn dst20_name_info(dvm_block: u64, name: &str, symbol: &str) -> Vec<(H256, H256)> {
    let contract_address = if dvm_block >= ain_cpp_imports::get_df23_height() {
        get_dst20_v2_contract().fixed_address
    } else {
        get_dst20_v1_contract().fixed_address
    };

    vec![
        (H256::from_low_u64_be(3), get_abi_encoded_string(name)),
        (H256::from_low_u64_be(4), get_abi_encoded_string(symbol)),
        (IMPLEMENTATION_SLOT, h160_to_h256(contract_address)),
    ]
}
