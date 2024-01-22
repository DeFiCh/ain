use std::{
    cmp::{max, Ordering},
    sync::Arc,
};

use ain_cpp_imports::Attributes;
use anyhow::format_err;
use ethereum::{BlockAny, TransactionAny};
use ethereum_types::U256;
use keccak_hash::H256;
use log::{debug, trace};
use statrs::statistics::{Data, OrderStatistics};

use crate::{
    storage::{traits::BlockStorage, Storage},
    EVMError, Result,
};

pub struct BlockService {
    storage: Arc<Storage>,
    starting_block_number: U256,
}

pub struct FeeHistoryData {
    pub oldest_block: U256,
    pub base_fee_per_gas: Vec<U256>,
    pub gas_used_ratio: Vec<f64>,
    pub reward: Option<Vec<Vec<U256>>>,
}

pub const INITIAL_BASE_FEE: U256 = U256([10_000_000_000, 0, 0, 0]); // wei
const MAX_BASE_FEE: U256 = crate::weiamount::MAX_MONEY_SATS;
const PRIORITY_FEE_ESTIMATION_BLOCK_RANGE: usize = 20;

/// Handles getting block data, and contains internal functions for block creation and fees.
impl BlockService {
    /// Create new [BlockService] with given [Storage].
    pub fn new(storage: Arc<Storage>) -> Result<Self> {
        let mut block_handler = Self {
            storage,
            starting_block_number: U256::zero(),
        };
        let (_, block_number) = block_handler
            .get_latest_block_hash_and_number()?
            .unwrap_or_default();

        block_handler.starting_block_number = block_number;
        debug!("Current block number is {:x?}", block_number);

        Ok(block_handler)
    }

    /// Get latest block number when struct was created.
    /// Used in `eth_syncing`.
    pub fn get_starting_block_number(&self) -> U256 {
        self.starting_block_number
    }

    /// Returns latest confirmed block hash and number.
    pub fn get_latest_block_hash_and_number(&self) -> Result<Option<(H256, U256)>> {
        let opt_block = self.storage.get_latest_block()?;
        let opt_hash_and_number = opt_block.map(|block| (block.header.hash(), block.header.number));
        Ok(opt_hash_and_number)
    }

    /// Returns state root from the latest confirmed block.
    pub fn get_latest_state_root(&self) -> Result<H256> {
        let state_root = self
            .storage
            .get_latest_block()?
            .map(|block| block.header.state_root)
            .unwrap_or_default();
        Ok(state_root)
    }

    /// Add new block to storage. This block must have passed validation before being added to storage. Once it is added to storage it becomes the latest confirmed block.
    pub fn connect_block(&self, block: &BlockAny) -> Result<()> {
        self.storage.put_latest_block(Some(block))?;
        self.storage.put_block(block)
    }

    /// Finds base fee for block based on parent gas usage. Can be used to find base fee for next block if latest block data is passed.
    pub fn get_base_fee(
        &self,
        parent_gas_used: u64,
        parent_gas_target: u64,
        parent_base_fee: U256,
        base_fee_max_change_denominator: U256,
        initial_base_fee: U256,
        max_base_fee: U256,
    ) -> Result<U256> {
        match parent_gas_used.cmp(&parent_gas_target) {
            Ordering::Equal => Ok(parent_base_fee),
            Ordering::Greater => {
                let gas_used_delta = parent_gas_used - parent_gas_target; // sub is safe due to cmp
                let base_fee_per_gas_delta = self.get_base_fee_per_gas_delta(
                    gas_used_delta,
                    parent_gas_target,
                    parent_base_fee,
                    base_fee_max_change_denominator,
                )?;
                Ok(parent_base_fee
                    .saturating_add(base_fee_per_gas_delta)
                    .min(max_base_fee))
            }
            Ordering::Less => {
                let gas_used_delta = parent_gas_target - parent_gas_used; // sub is safe due to cmp
                let base_fee_per_gas_delta = self.get_base_fee_per_gas_delta(
                    gas_used_delta,
                    parent_gas_target,
                    parent_base_fee,
                    base_fee_max_change_denominator,
                )?;
                Ok(parent_base_fee
                    .saturating_sub(base_fee_per_gas_delta)
                    .max(initial_base_fee))
            }
        }
    }

    fn get_base_fee_per_gas_delta(
        &self,
        gas_used_delta: u64,
        parent_gas_target: u64,
        parent_base_fee: U256,
        base_fee_max_change_denominator: U256,
    ) -> Result<U256> {
        Ok(max(
            parent_base_fee
                .checked_mul(gas_used_delta.into())
                .and_then(|x| x.checked_div(parent_gas_target.into()))
                .and_then(|x| x.checked_div(base_fee_max_change_denominator))
                .ok_or_else(|| {
                    format_err!("Unsatisfied bounds calculating base_fee_per_gas_delta")
                })?,
            U256::one(),
        ))
    }

    /// Read attributes from DVM mnview. Falls back to defaults if `mnview_ptr` is `None`.
    pub fn get_attribute_vals(&self, mnview_ptr: Option<usize>) -> Attributes {
        ain_cpp_imports::get_attribute_values(mnview_ptr)
    }

    /// Calculate base fee from parent block hash and gas target factor.
    ///
    /// # Arguments
    /// * `parent_hash` - Parent block hash
    /// * `block_gas_target_factor` - Determines the gas target for the previous block using the formula `gas target = gas limit / target factor`
    pub fn calculate_base_fee(
        &self,
        parent_hash: H256,
        block_gas_target_factor: u64,
    ) -> Result<U256> {
        // constants
        let base_fee_max_change_denominator = U256::from(8);

        // first block has 1 gwei base fee
        if parent_hash == H256::zero() {
            return Ok(INITIAL_BASE_FEE);
        }

        // get parent gas usage.
        // Ref: https://eips.ethereum.org/EIPS/eip-1559#:~:text=fee%20is%20correct-,if%20INITIAL_FORK_BLOCK_NUMBER%20%3D%3D%20block.number%3A,-expected_base_fee_per_gas%20%3D%20INITIAL_BASE_FEE
        let parent_block = self
            .storage
            .get_block_by_hash(&parent_hash)?
            .ok_or(format_err!("Parent block not found"))?;
        let parent_base_fee = parent_block.header.base_fee;
        let parent_gas_used = u64::try_from(parent_block.header.gas_used)?;
        let parent_gas_target =
            u64::try_from(parent_block.header.gas_limit / block_gas_target_factor)?; // safe to use normal division since we know block_gas_limit_factor is non-zero
        self.get_base_fee(
            parent_gas_used,
            parent_gas_target,
            parent_base_fee,
            base_fee_max_change_denominator,
            INITIAL_BASE_FEE,
            MAX_BASE_FEE,
        )
    }

    /// Gets base fee, gas usage ratio and priority fees for a range of blocks. Used in `eth_feeHistory`.
    ///
    /// # Arguments
    /// * `block_count` - Number of blocks' data to return.
    /// * `first_block` - Block number of first block.
    /// * `priority_fee_percentile` - Vector of percentiles. This will return percentile priority fee for all blocks. e.g. [20, 50, 70] will return 20th, 50th and 70th percentile priority fee for every block.
    /// * `block_gas_target_factor` - Determines gas target. Used when latest `block_count` blocks are queried.
    pub fn fee_history(
        &self,
        block_count: usize,
        first_block: U256,
        priority_fee_percentile: Vec<usize>,
        block_gas_target_factor: u64,
    ) -> Result<FeeHistoryData> {
        let mut blocks = Vec::with_capacity(block_count);
        let mut block_number = first_block;

        for _ in 1..=block_count {
            let block = match self.storage.get_block_by_number(&block_number)? {
                None => Err(format_err!("Block {} out of range", block_number)),
                Some(block) => Ok(block),
            }?;

            blocks.push(block);

            block_number = block_number
                .checked_sub(U256::one())
                .ok_or_else(|| format_err!("block_number underflow"))?;
        }

        let oldest_block = blocks.last().unwrap().header.number;

        let (mut base_fee_per_gas, mut gas_used_ratio) = blocks.iter().try_fold(
            (Vec::new(), Vec::new()),
            |(mut base_fee_per_gas, mut gas_used_ratio), block| {
                trace!("[fee_history] Processing block {}", block.header.number);
                let base_fee = block.header.base_fee;

                let gas_ratio = if block.header.gas_limit == U256::zero() {
                    f64::default() // empty block
                } else {
                    u64::try_from(block.header.gas_used)? as f64
                        / u64::try_from(block.header.gas_limit)? as f64 // safe due to check
                };

                base_fee_per_gas.push(base_fee);
                gas_used_ratio.push(gas_ratio);

                Ok::<_, EVMError>((base_fee_per_gas, gas_used_ratio))
            },
        )?;

        let reward = if priority_fee_percentile.is_empty() {
            None
        } else {
            let mut eip_transactions = Vec::new();

            for block in blocks {
                let mut block_eip_transaction = Vec::new();
                for tx in block.transactions {
                    match tx {
                        TransactionAny::Legacy(_) | TransactionAny::EIP2930(_) => {
                            continue;
                        }
                        TransactionAny::EIP1559(t) => {
                            block_eip_transaction.push(t);
                        }
                    }
                }
                block_eip_transaction
                    .sort_by(|a, b| a.max_priority_fee_per_gas.cmp(&b.max_priority_fee_per_gas));
                eip_transactions.push(block_eip_transaction);
            }

            /*
                TODO: assumption here is that max priority fee = priority fee paid, however
                priority fee can be lower if gas costs hit max_fee_per_gas.
                we will need to check the base fee paid to get the actual priority fee paid
            */

            let mut reward = Vec::new();

            for block_eip_tx in eip_transactions {
                if block_eip_tx.is_empty() {
                    reward.push(vec![U256::zero()]);
                    continue;
                }

                let mut block_rewards = Vec::new();
                let priority_fees = block_eip_tx
                    .iter()
                    .map(|tx| Ok(u64::try_from(tx.max_priority_fee_per_gas)? as f64))
                    .collect::<Result<Vec<f64>>>()?;
                let mut data = Data::new(priority_fees);

                for pct in &priority_fee_percentile {
                    block_rewards.push(U256::from(data.percentile(*pct).floor() as u64));
                }

                reward.push(block_rewards);
            }

            reward.reverse();
            Some(reward)
        };

        // add another entry for baseFeePerGas
        let next_block_base_fee = match self.storage.get_block_by_number(
            &(first_block
                .checked_add(U256::one())
                .ok_or_else(|| format_err!("Block number overflow"))?),
        )? {
            None => {
                // get one block earlier (this should exist)
                let block = self
                    .storage
                    .get_block_by_number(&first_block)?
                    .ok_or_else(|| format_err!("Block {} out of range", first_block))?;
                self.calculate_base_fee(block.header.hash(), block_gas_target_factor)?
            }
            Some(block) => self.calculate_base_fee(block.header.hash(), block_gas_target_factor)?,
        };

        base_fee_per_gas.reverse();
        base_fee_per_gas.push(next_block_base_fee);

        gas_used_ratio.reverse();

        Ok(FeeHistoryData {
            oldest_block,
            base_fee_per_gas,
            gas_used_ratio,
            reward,
        })
    }

    /// Returns the nth percentile (default: 60) priority fee for the last 20 blocks.
    /// Ref: https://github.com/ethereum/go-ethereum/blob/c57b3436f4b8aae352cd69c3821879a11b5ee0fb/eth/ethconfig/config.go#L41
    pub fn suggested_priority_fee(&self, curr_block: U256) -> Result<U256> {
        let percentile = ain_cpp_imports::get_suggested_priority_fee_percentile();
        let mut blocks = Vec::with_capacity(PRIORITY_FEE_ESTIMATION_BLOCK_RANGE);
        let mut block_num = if let Some(block_num) = highest_block.checked_sub(PRIORITY_FEE_ESTIMATION_BLOCK_RANGE) {
            block_num
                .checked_add(U256::one())
                .ok_or(format_err!("Block number overflow"))?
        } else {
            U256::zero()
        };

        while block_num <= curr_block {
            let block = self
                .storage
                .get_block_by_number(&block_num)?
                .ok_or(format_err!("Block {:#?} out of range", block_num))?;
            blocks.push(block.clone());
            block_num = block_num
                .checked_add(U256::one())
                .ok_or(format_err!("Next block number overflow"))?;
        }

        let mut priority_fees = Vec::new();
        for block in blocks {
            let base_fee = block.header.base_fee;
            for tx in block.transactions {
                let tx_rewards = SignedTx::try_from(tx)?.effective_priority_fee_per_gas(base_fee)?;
                priority_fees.push(tx_rewards);
            }
        }
        let mut data = Data::new(priority_fees);
        Ok(U256::from(data.percentile(percentile).ceil() as u64))
    }

    /// Calculate suggested legacy fee from latest base fee and suggested priority fee.
    pub fn get_legacy_fee(&self, curr_block: U256) -> Result<U256> {
        let priority_fee = self.suggested_priority_fee(curr_block)?;
        let base_fee = self
            .storage
            .get_latest_block()?
            .expect("Unable to get latest block")
            .header
            .base_fee;

        Ok(base_fee
            .checked_add(priority_fee)
            .ok_or_else(|| format_err!("Legacy fee overflow"))?)
    }
}

#[cfg(test)]
mod tests {
    // use super::*;

    // #[test]
    // fn test_base_fee_equal() {
    //     let block = BlockService::new(Arc::new(Storage::new()?)).unwrap();
    //     assert_eq!(
    //         U256::from(20_000_000_000u64),
    //         block.base_fee_calculation(
    //             15_000_000,
    //             15_000_000,
    //             U256::from(20_000_000_000u64),
    //             U256::from(8),
    //             U256::from(10_000_000_000u64)
    //         )
    //     )
    // }

    // #[test]
    // fn test_base_fee_max_increase() {
    //     let block = BlockService::new(Arc::new(Storage::new()?)).unwrap();
    //     assert_eq!(
    //         U256::from(22_500_000_000u64), // should increase by 12.5%
    //         block.base_fee_calculation(
    //             30_000_000,
    //             15_000_000,
    //             U256::from(20_000_000_000u64),
    //             U256::from(8),
    //             U256::from(10_000_000_000u64)
    //         )
    //     )
    // }

    // #[test]
    // fn test_base_fee_increase() {
    //     let block = BlockService::new(Arc::new(Storage::new()?)).unwrap();
    //     assert_eq!(
    //         U256::from(20_833_333_333u64), // should increase by ~4.15%
    //         block.base_fee_calculation(
    //             20_000_000,
    //             15_000_000,
    //             U256::from(20_000_000_000u64),
    //             U256::from(8),
    //             U256::from(10_000_000_000u64)
    //         )
    //     )
    // }

    // #[test]
    // fn test_base_fee_max_decrease() {
    //     let block = BlockService::new(Arc::new(Storage::new()?)).unwrap();
    //     assert_eq!(
    //         U256::from(17_500_000_000u64), // should decrease by 12.5%
    //         block.base_fee_calculation(
    //             0,
    //             15_000_000,
    //             U256::from(20_000_000_000u64),
    //             U256::from(8),
    //             U256::from(10_000_000_000u64)
    //         )
    //     )
    // }

    // #[test]
    // fn test_base_fee_decrease() {
    //     let block = BlockService::new(Arc::new(Storage::new()?)).unwrap();
    //     assert_eq!(
    //         U256::from(19_166_666_667u64), // should increase by ~4.15%
    //         block.base_fee_calculation(
    //             10_000_000,
    //             15_000_000,
    //             U256::from(20_000_000_000u64),
    //             U256::from(8),
    //             U256::from(10_000_000_000u64)
    //         )
    //     )
    // }
}
