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
    transaction::SignedTx,
    Result,
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

pub const MAX_REWARD_PERCENTAGE: usize = 100;
pub const MIN_BLOCK_COUNT_RANGE: U256 = U256::one();
pub const MAX_BLOCK_COUNT_RANGE: U256 = U256([1024, 0, 0, 0]);

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

    /// Add new block to storage. This block must have passed validation before being added to storage.
    /// Once it is added to storage it becomes the latest confirmed block.
    pub fn connect_block(&self, block: &BlockAny) -> Result<()> {
        self.storage.put_latest_block(Some(block))?;
        self.storage.put_block(block)
    }

    /// Finds base fee for block based on parent gas usage. Can be used to find base fee for next block
    /// if latest block data is passed.
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
    /// * `block_gas_target_factor` - Determines the gas target for the previous block using the formula:
    ///   `gas target = gas limit / target factor`
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
        // safe to use normal division since we know block_gas_limit_factor is non-zero
        let parent_gas_target =
            u64::try_from(parent_block.header.gas_limit / block_gas_target_factor)?;
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
    /// * `block_count` - Number of blocks' data to return. Between 1 and 1024 blocks can be requested in a single query.
    /// * `highest_block` - Block number of highest block.
    /// * `reward_percentile` - List of percilt values with monotonic increase in value. The transactions will be ranked
    ///                         effective tip per gas for each block in the requested range, and the corresponding effective
    ///                         tip for the percentile will be calculated while taking gas consumption into consideration.
    /// * `block_gas_target_factor` - Determines gas target. Used when latest `block_count` blocks are queried.
    pub fn fee_history(
        &self,
        block_count: U256,
        highest_block: U256,
        reward_percentile: Vec<usize>,
        block_gas_target_factor: u64,
    ) -> Result<FeeHistoryData> {
        // Validate block_count input
        if block_count < MIN_BLOCK_COUNT_RANGE {
            return Err(format_err!(
                "Block count requested smaller than minimum allowed range of {}",
                MIN_BLOCK_COUNT_RANGE,
            )
            .into());
        }
        if block_count > MAX_BLOCK_COUNT_RANGE {
            return Err(format_err!(
                "Block count requested larger than maximum allowed range of {}",
                MAX_BLOCK_COUNT_RANGE,
            )
            .into());
        }

        // Validate reward_percentile input
        if reward_percentile.len() > MAX_REWARD_PERCENTAGE {
            return Err(format_err!(
                "List of percentile values exceeds maximum allowed size of {}",
                MAX_REWARD_PERCENTAGE,
            )
            .into());
        }
        let mut prev_percentile = 0;
        for percentile in &reward_percentile {
            if *percentile > MAX_REWARD_PERCENTAGE {
                return Err(format_err!(
                    "Percentile value more than inclusive range of {}",
                    MAX_REWARD_PERCENTAGE,
                )
                .into());
            }
            if prev_percentile > *percentile {
                return Err(format_err!(
                    "List of percentile values are not monotonically increasing"
                )
                .into());
            }
            prev_percentile = *percentile;
        }

        let mut blocks = Vec::with_capacity(MAX_BLOCK_COUNT_RANGE.as_usize());
        let mut block_num = if let Some(block_num) = highest_block.checked_sub(block_count) {
            block_num
                .checked_add(U256::one())
                .ok_or(format_err!("Block number overflow"))?
        } else {
            U256::zero()
        };

        while block_num <= highest_block {
            let block = self
                .storage
                .get_block_by_number(&block_num)?
                .ok_or(format_err!("Block {:#?} out of range", block_num))?;
            blocks.push(block);

            block_num = block_num
                .checked_add(U256::one())
                .ok_or(format_err!("Next block number overflow"))?;
        }

        // Set oldest block number
        let oldest_block = blocks
            .first()
            .ok_or(format_err!("Unable to fetch oldest block"))?
            .header
            .number;

        let mut base_fee_per_gas = Vec::with_capacity(MAX_BLOCK_COUNT_RANGE.as_usize());
        let mut gas_used_ratio = Vec::with_capacity(MAX_BLOCK_COUNT_RANGE.as_usize());
        let mut rewards = Vec::with_capacity(MAX_BLOCK_COUNT_RANGE.as_usize());
        for block in blocks.clone() {
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

            let mut block_tx_rewards = Vec::with_capacity(block.transactions.len());
            for tx in block.transactions {
                let tx_rewards =
                    SignedTx::try_from(tx)?.effective_priority_fee_per_gas(base_fee)?;
                block_tx_rewards.push(u64::try_from(tx_rewards)? as f64);
            }

            let reward = if block_tx_rewards.is_empty() {
                vec![U256::zero(); reward_percentile.len()]
            } else {
                let mut r = Vec::with_capacity(reward_percentile.len());
                let mut data = Data::new(block_tx_rewards);
                for percent in &reward_percentile {
                    r.push(U256::from(data.percentile(*percent).floor() as u64));
                }
                r
            };
            rewards.push(reward);
        }

        // Add next block entry
        let next_block = highest_block
            .checked_add(U256::one())
            .ok_or(format_err!("Next block number overflow"))?;
        let next_block_base_fee = match self.storage.get_block_by_number(&next_block)? {
            Some(block) => block.header.base_fee,
            None => {
                let highest_block_info = blocks
                    .last()
                    .ok_or(format_err!("Unable to fetch highest block"))?;
                self.calculate_base_fee(highest_block_info.header.hash(), block_gas_target_factor)?
            }
        };

        base_fee_per_gas.push(next_block_base_fee);
        Ok(FeeHistoryData {
            oldest_block,
            base_fee_per_gas,
            gas_used_ratio,
            reward: if rewards.is_empty() {
                None
            } else {
                Some(rewards)
            },
        })
    }

    /// Returns the 60th percentile priority fee for the last 20 blocks. [Reference](https://github.com/ethereum/go-ethereum/blob/c57b3436f4b8aae352cd69c3821879a11b5ee0fb/eth/ethconfig/config.go#L41)
    // TODO: these should be configurable by the user
    pub fn suggested_priority_fee(&self) -> Result<U256> {
        let mut blocks = Vec::with_capacity(20);
        let block = self
            .storage
            .get_latest_block()?
            .ok_or(format_err!("Unable to find latest block"))?;

        blocks.push(block.clone());
        let mut parent_hash = block.header.parent_hash;

        while blocks.len() <= blocks.capacity() {
            match self.storage.get_block_by_hash(&parent_hash)? {
                Some(block) => {
                    blocks.push(block.clone());
                    parent_hash = block.header.parent_hash;
                }
                None => break,
            }
        }

        /*
            TODO: assumption here is that max priority fee = priority fee paid, however
            priority fee can be lower if gas costs hit max_fee_per_gas.
            we will need to check the base fee paid to get the actual priority fee paid
        */

        let mut priority_fees = Vec::new();

        for block in blocks {
            for tx in block.transactions {
                match tx {
                    TransactionAny::Legacy(_) | TransactionAny::EIP2930(_) => {
                        continue;
                    }
                    TransactionAny::EIP1559(t) => {
                        priority_fees.push(u64::try_from(t.max_priority_fee_per_gas)? as f64);
                    }
                }
            }
        }

        priority_fees.sort_by(|a, b| a.partial_cmp(b).expect("Invalid f64 value"));
        let mut data = Data::new(priority_fees);

        Ok(U256::from(data.percentile(60).ceil() as u64))
    }

    /// Calculate suggested legacy fee from latest base fee and suggested priority fee.
    pub fn get_legacy_fee(&self) -> Result<U256> {
        let priority_fee = self.suggested_priority_fee()?;
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
