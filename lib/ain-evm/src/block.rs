use std::{
    cmp::{max, min, Ordering},
    sync::Arc,
};

use ain_cpp_imports::Attributes;
use anyhow::format_err;
use ethereum::BlockAny;
use ethereum_types::U256;
use keccak_hash::H256;
use log::trace;
use parking_lot::Mutex;

use crate::{
    storage::{
        traits::{BlockStorage, ReceiptStorage},
        Storage,
    },
    transaction::SignedTx,
    Result,
};

pub const INITIAL_BASE_FEE: U256 = U256([10_000_000_000, 0, 0, 0]); // wei
pub const MAX_BASE_FEE: U256 = crate::weiamount::MAX_MONEY_SATS;

pub const MIN_PERCENTAGE: i64 = 0;
pub const MAX_PERCENTAGE: i64 = 100;
pub const MAX_REWARD_PERCENTAGE: usize = 100;
pub const MIN_BLOCK_COUNT_RANGE: U256 = U256::one();
pub const MAX_BLOCK_COUNT_RANGE: U256 = U256([1024, 0, 0, 0]);
pub const DEFAULT_PRIORITY_FEE_BLOCK_RANGE: U256 = U256([20, 0, 0, 0]);

pub struct BlockService {
    storage: Arc<Storage>,
    starting_block_number: U256,
    last_suggested_fee_tip: Mutex<Option<SuggestedFeeTip>>,
}

pub struct SuggestedFeeTip {
    tip: U256,
    suggested_fee: U256,
}

pub struct FeeHistoryData {
    pub oldest_block: U256,
    pub base_fee_per_gas: Vec<U256>,
    pub gas_used_ratio: Vec<f64>,
    pub reward: Option<Vec<Vec<U256>>>,
}

/// Backend that handles all necessary APIs for chain data and fee services.
impl BlockService {
    /// Create new [BlockService] with given [Storage].
    pub fn new(storage: Arc<Storage>) -> Result<Self> {
        let mut block_handler = Self {
            storage,
            starting_block_number: U256::zero(),
            last_suggested_fee_tip: Mutex::new(None),
        };
        let (_, block_number) = block_handler
            .get_latest_block_hash_and_number()?
            .unwrap_or_default();

        block_handler.starting_block_number = block_number;

        Ok(block_handler)
    }
}

// Methods for updating and getting block data
impl BlockService {
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

    /// Read attributes from DVM mnview. Falls back to defaults if `mnview_ptr` is `None`.
    pub fn get_attribute_vals(&self, mnview_ptr: Option<usize>) -> Attributes {
        ain_cpp_imports::get_attribute_values(mnview_ptr)
    }
}

// Methods for calculating the next block's base fees
impl BlockService {
    /// API to calculate base fee from parent block hash and gas target factor.
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

    pub fn clear_last_suggested_fee_tip_cache(&self) {
        let mut last_suggested_fee_tip = self.last_suggested_fee_tip.lock();
        *last_suggested_fee_tip = None;
    }
}

// Methods for gas prices RPC pipelines
impl BlockService {
    /// Returns a recommended suitable priority fee per gas that is suitable for newly created transactions, based
    /// on the content of recent blocks. The suggested gas price enables newly created transactions to have a very
    /// high chance to be included in the following blocks.
    ///
    /// To configure a custom nth percentile parameter, set -evmtxpriorityfeepercentile=n on startup.
    /// Otherwise, the default nth percentile parameter is set at 60% for a default block range of 20.
    ///
    /// Note for legacy transactions and the legacy eth_gasPrice RPC call, it will be necessary to add the base fee.
    /// Ref: https://github.com/ethereum/go-ethereum/blob/master/eth/gasprice/gasprice.go
    pub fn suggest_priority_fee(&self) -> Result<U256> {
        let mut percentile = ain_cpp_imports::get_suggested_priority_fee_percentile();
        percentile = max(percentile, MIN_PERCENTAGE);
        percentile = min(percentile, MAX_PERCENTAGE);
        if let Some(curr_block) = self.storage.get_latest_block()? {
            let curr_block_num = curr_block.header.number;
            {
                let last_suggested_fee_tip = self.last_suggested_fee_tip.lock();
                if let Some(last_suggested_fee_tip) = &*last_suggested_fee_tip {
                    if last_suggested_fee_tip.tip == curr_block_num {
                        return Ok(last_suggested_fee_tip.suggested_fee);
                    }
                }
            }

            // Cache miss, calculate new tip's suggested fee
            let mut block_num =
                if let Some(num) = curr_block_num.checked_sub(DEFAULT_PRIORITY_FEE_BLOCK_RANGE) {
                    num.checked_add(U256::one())
                        .ok_or(format_err!("Block number overflow"))?
                } else {
                    U256::zero()
                };

            let mut blocks = Vec::with_capacity(DEFAULT_PRIORITY_FEE_BLOCK_RANGE.as_usize());
            while block_num <= curr_block_num {
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
                    priority_fees
                        .push(SignedTx::try_from(tx)?.effective_priority_fee_per_gas(base_fee)?);
                }
            }

            let suggested_fee = if priority_fees.is_empty() {
                // Empty blocks, default to zero
                U256::zero()
            } else {
                priority_fees.sort();
                // Safe since max possible txs in 20 blocks is within i64 limits
                let percent_idx =
                    max(((priority_fees.len() as i64) - 1) * percentile / 100, 0) as usize;
                priority_fees[percent_idx]
            };

            // Update cache
            {
                let mut last_suggested_fee_tip = self.last_suggested_fee_tip.lock();
                *last_suggested_fee_tip = Some(SuggestedFeeTip {
                    tip: curr_block_num,
                    suggested_fee,
                });
            }
            Ok(suggested_fee)
        } else {
            // No genesis block, default to zero
            Ok(U256::zero())
        }
    }

    /// Returns a recommended suitable legacy fee per gas from latest base fee and the suggested priority fee,
    /// based on the content of recent blocks. The suggested gas price enables newly created legacy transactions
    /// to have a very high chance to be included in the following blocks.
    pub fn suggest_legacy_fee(&self) -> Result<U256> {
        let priority_fee = self.suggest_priority_fee()?;
        let base_fee = self
            .storage
            .get_latest_block()?
            .map(|block| block.header.base_fee)
            .unwrap_or(INITIAL_BASE_FEE);

        Ok(base_fee
            .checked_add(priority_fee)
            .ok_or_else(|| format_err!("Legacy fee overflow"))?)
    }

    /// FeeHistory returns data relevant for fee estimation based on the specified range of blocks. The range can be
    /// specified either with absolute block numbers or ending with the latest or pending block. Backend do not support
    /// gathering data from the pending block. The first block of the actually processed range is returned to avoid
    /// ambiguity when parts of the requested range are not available or when the head has changed during processing
    /// this request.
    ///
    /// # Arguments
    /// * `block_count` - Number of blocks to be returned. (1-1024 blocks can be requested in a single query)
    /// * `highest_block` - Block number of highest block.
    /// * `reward_percentile` - A monotonically increasing list of percentile values to sample from each block's
    ///    effective priority fees per gas in ascending order, weighted by gas used.
    /// * `block_gas_target_factor` - Determines gas target. Used when latest `block_count` blocks are queried.
    ///
    /// # Returns
    /// * `oldestBlock` - Lowest number of block of the returned range.
    /// * `baseFee` - An array of block base fees per gas, including an extra block value. The extra value is the next
    ///   block after the newest block in the returned range.
    /// * `gasUsedRatio` - An array of block gas used ratio, caculated as the ratio of gasUsed / gasLimit.
    /// * `reward` - The requested percentiles of effective priority fees per gas of transactions in each block, sorted
    ///   in ascending order and weighted by gas used.
    pub fn fee_history(
        &self,
        block_count: U256,
        highest_block: U256,
        reward_percentile: Vec<i64>,
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
            if *percentile < MIN_PERCENTAGE {
                return Err(format_err!(
                    "Percentile value less than inclusive range of {}",
                    MIN_PERCENTAGE,
                )
                .into());
            }
            if *percentile > MAX_PERCENTAGE {
                return Err(format_err!(
                    "Percentile value more than inclusive range of {}",
                    MAX_PERCENTAGE,
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

        let mut result = FeeHistoryData {
            oldest_block: blocks
                .first()
                .ok_or(format_err!("Unable to fetch oldest block"))?
                .header
                .number,
            base_fee_per_gas: Vec::with_capacity(MAX_BLOCK_COUNT_RANGE.as_usize()),
            gas_used_ratio: Vec::with_capacity(MAX_BLOCK_COUNT_RANGE.as_usize()),
            reward: if reward_percentile.is_empty() {
                None
            } else {
                Some(Vec::with_capacity(MAX_BLOCK_COUNT_RANGE.as_usize()))
            },
        };

        for block in blocks.clone() {
            trace!("[fee_history] Processing block {}", block.header.number);
            let base_fee = block.header.base_fee;
            let gas_ratio = if block.header.gas_limit == U256::zero() {
                f64::default() // empty block
            } else {
                u64::try_from(block.header.gas_used)? as f64
                    / u64::try_from(block.header.gas_limit)? as f64 // safe due to check
            };
            result.base_fee_per_gas.push(base_fee);
            result.gas_used_ratio.push(gas_ratio);

            if let Some(rewards) = &mut result.reward {
                // Retrieve gas used and effective gas price from txs
                let mut sorter = Vec::with_capacity(block.transactions.len());
                let mut cumulative_gas = U256::zero();
                for tx in block.transactions {
                    let receipt = self
                        .storage
                        .get_receipt(&tx.hash())?
                        .ok_or(format_err!("Unable to fetch tx receipt"))?;
                    let gas_used = receipt.cumulative_gas.saturating_sub(cumulative_gas);
                    cumulative_gas = cumulative_gas.saturating_add(gas_used);
                    sorter.push((
                        SignedTx::try_from(tx)?.effective_priority_fee_per_gas(base_fee)?,
                        gas_used,
                    ));
                }
                sorter.sort_by(|a, b| a.0.cmp(&b.0));

                let reward = if sorter.is_empty() {
                    vec![U256::zero(); reward_percentile.len()]
                } else {
                    let mut tx_index = 0;
                    let mut sum_gas_used = sorter.first().map(|e| e.1).unwrap_or_default();
                    let mut r = Vec::with_capacity(reward_percentile.len());
                    for percent in &reward_percentile {
                        let threshold_gas_used = block
                            .header
                            .gas_used
                            .saturating_mul(U256::from(*percent))
                            .checked_div(U256([100, 0, 0, 0]))
                            .unwrap_or_default();
                        while sum_gas_used < threshold_gas_used && tx_index < (sorter.len() - 1) {
                            tx_index += 1;
                            sum_gas_used = sum_gas_used.saturating_add(sorter[tx_index].1);
                        }
                        r.push(sorter[tx_index].0);
                    }
                    r
                };
                rewards.push(reward);
            }
        }

        // Add next block base fee entry
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
        result.base_fee_per_gas.push(next_block_base_fee);
        Ok(result)
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
