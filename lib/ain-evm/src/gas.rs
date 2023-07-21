use crate::transaction::SignedTx;
use ethereum::{AccessList, TransactionAction};
use ethereum_types::U256;

use anyhow::anyhow;
use log::debug;
use std::error::Error;

pub const MIN_GAS_PER_TX: u64 = 21_000;
pub const MIN_GAS_PER_CONTRACT_CREATION_TX: u64 = 53_000;
pub const TX_DATA_NON_ZERO_GAS: u64 = 68;
pub const TX_DATA_ZERO_GAS: u64 = 4;
pub const TX_ACCESS_LIST_ADDRESS_GAS: u64 = 68;
pub const TX_ACCESS_LIST_STORAGE_KEY_GAS: u64 = 1_900;
pub const INIT_CODE_WORD_GAS: u64 = 2;

// Returns the ceiled word size required for init code payment calculation.
pub fn to_word_size(size: u64) -> u64 {
    if size > (u64::MAX - 31) {
        return (u64::MAX / 32) + 1;
    }
    (size + 31) / 32
}

pub fn count_storage_keys(access_list: AccessList) -> usize {
	access_list.iter().map(|item| item.storage_keys.len()).sum()
}

pub fn check_tx_intrinsic_gas(signed_tx: &SignedTx) -> Result<U256, Box<dyn Error>> {
    let mut gas = 0;
    match signed_tx.action() {
        TransactionAction::Call(_) => {
            gas += MIN_GAS_PER_TX;
            debug!("[check_tx_intrinsic_gas] gas after adding minimum gas per tx: {:#?}", gas);
        }
        TransactionAction::Create => {
            gas += MIN_GAS_PER_CONTRACT_CREATION_TX;
            debug!("[check_tx_intrinsic_gas] gas after adding minimum gas per creation tx: {:#?}", gas);
        }
    }

    let data_len = u64::try_from(signed_tx.data().len())?;
    if data_len > 0 {
        //  Zero and non-zero bytes are priced differently
        let zero_data_len = u64::try_from(signed_tx.data().iter().filter(|v| **v == 0).count())?;
        let non_zero_data_len = data_len - zero_data_len;

        // Make sure we do not exceed uint64 for all data combinations
        if ((u64::MAX - gas) / TX_DATA_ZERO_GAS) < zero_data_len {
            debug!("[check_tx_intrinsic_gas] gas overflow (zero gas)");
            return Err(anyhow!("gas overflow (zero gas)").into());
        }
        gas += zero_data_len * TX_DATA_ZERO_GAS;
        debug!("[check_tx_intrinsic_gas] gas after adding zero data cost: {:#?}", gas);

        if ((u64::MAX - gas) / TX_DATA_NON_ZERO_GAS) < non_zero_data_len {
            debug!("[check_tx_intrinsic_gas] gas overflow (non-zero gas)");
            return Err(anyhow!("gas overflow (non-zero gas)").into());
        }
        gas += non_zero_data_len * TX_DATA_NON_ZERO_GAS;
        debug!("[check_tx_intrinsic_gas] gas after adding non-zero data cost: {:#?}", gas);

        match signed_tx.action() {
            TransactionAction::Create => {
                let len_words = to_word_size(data_len);
                if ((u64::MAX - gas) / INIT_CODE_WORD_GAS) < len_words {
                    debug!("[check_tx_intrinsic_gas] gas overflow (init-code cost)");
                    return Err(anyhow!("gas overflow (init-code cost)").into());
                }
                gas += len_words * INIT_CODE_WORD_GAS;
                debug!("[check_tx_intrinsic_gas] gas after adding init-code cost: {:#?}", gas);
            }
            _ => (),
        }

        let access_list = signed_tx.access_list();
        if access_list.len() > 0 {
            gas += u64::try_from(access_list.len())? * TX_ACCESS_LIST_ADDRESS_GAS;
            debug!("[check_tx_intrinsic_gas] gas after adding access list cost: {:#?}", gas);
            gas += u64::try_from(count_storage_keys(access_list))? * TX_ACCESS_LIST_STORAGE_KEY_GAS;
            debug!("[check_tx_intrinsic_gas] gas after adding storage list cost: {:#?}", gas);
        }
    }
    Ok(U256::from(gas))
}