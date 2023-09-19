use std::{
    cmp::{max, Ordering},
    sync::Arc,
};

use anyhow::format_err;
use ethereum::{BlockAny, TransactionAny};
use ethereum_types::U256;
use keccak_hash::H256;
use log::{debug, trace};
use statrs::statistics::{Data, OrderStatistics};

use crate::{
    storage::{traits::BlockStorage, Storage},
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

impl BlockService {
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

    pub fn get_starting_block_number(&self) -> U256 {
        self.starting_block_number
    }

    pub fn get_latest_block_hash_and_number(&self) -> Result<Option<(H256, U256)>> {
        let opt_block = self.storage.get_latest_block()?;
        let opt_hash_and_number = opt_block.map(|block| (block.header.hash(), block.header.number));
        Ok(opt_hash_and_number)
    }

    pub fn get_latest_state_root(&self) -> Result<H256> {
        let state_root = self
            .storage
            .get_latest_block()?
            .map(|block| block.header.state_root)
            .unwrap_or_default();
        Ok(state_root)
    }

    pub fn connect_block(&self, block: &BlockAny) -> Result<()> {
        self.storage.put_latest_block(Some(block))?;
        self.storage.put_block(block)
    }

    pub fn base_fee_calculation(
        &self,
        parent_gas_used: u64,
        parent_gas_target: u64,
        parent_base_fee: U256,
        base_fee_max_change_denominator: U256,
        initial_base_fee: U256,
    ) -> U256 {
        match parent_gas_used.cmp(&parent_gas_target) {
            Ordering::Equal => parent_base_fee,
            Ordering::Greater => {
                let gas_used_delta = parent_gas_used - parent_gas_target;
                let base_fee_per_gas_delta = max(
                    parent_base_fee * gas_used_delta
                        / parent_gas_target
                        / base_fee_max_change_denominator,
                    U256::one(),
                );

                max(parent_base_fee + base_fee_per_gas_delta, initial_base_fee)
            }
            Ordering::Less => {
                let gas_used_delta = parent_gas_target - parent_gas_used;
                let base_fee_per_gas_delta = parent_base_fee * gas_used_delta
                    / parent_gas_target
                    / base_fee_max_change_denominator;

                max(parent_base_fee - base_fee_per_gas_delta, initial_base_fee)
            }
        }
    }

    pub fn get_base_fee(
        &self,
        parent_gas_used: u64,
        parent_gas_target: u64,
        parent_base_fee: U256,
        base_fee_max_change_denominator: U256,
        initial_base_fee: U256,
    ) -> U256 {
        self.base_fee_calculation(
            parent_gas_used,
            parent_gas_target,
            parent_base_fee,
            base_fee_max_change_denominator,
            initial_base_fee,
        )
    }

    pub fn calculate_base_fee(&self, parent_hash: H256) -> Result<U256> {
        // constants
        let base_fee_max_change_denominator = U256::from(8);
        let elasticity_multiplier = U256::from(2);

        // first block has 1 gwei base fee
        if parent_hash == H256::zero() {
            return Ok(INITIAL_BASE_FEE);
        }

        // get parent gas usage,
        // https://eips.ethereum.org/EIPS/eip-1559#:~:text=fee%20is%20correct-,if%20INITIAL_FORK_BLOCK_NUMBER%20%3D%3D%20block.number%3A,-expected_base_fee_per_gas%20%3D%20INITIAL_BASE_FEE
        let parent_block = self
            .storage
            .get_block_by_hash(&parent_hash)?
            .ok_or(format_err!("Parent block not found"))?;
        let parent_base_fee = parent_block.header.base_fee;
        let parent_gas_used = parent_block.header.gas_used.as_u64();
        let parent_gas_target =
            parent_block.header.gas_limit.as_u64() / elasticity_multiplier.as_u64();

        Ok(self.get_base_fee(
            parent_gas_used,
            parent_gas_target,
            parent_base_fee,
            base_fee_max_change_denominator,
            INITIAL_BASE_FEE,
        ))
    }

    pub fn calculate_next_block_base_fee(&self) -> Result<U256> {
        let current_block_data = self.get_latest_block_hash_and_number()?;
        let current_block_hash = match current_block_data {
            None => H256::zero(),
            Some((hash, _)) => hash,
        };

        self.calculate_base_fee(current_block_hash)
    }

    pub fn fee_history(
        &self,
        block_count: usize,
        first_block: U256,
        priority_fee_percentile: Vec<usize>,
    ) -> Result<FeeHistoryData> {
        let mut blocks = Vec::with_capacity(block_count);
        let mut block_number = first_block;

        for _ in 1..=block_count {
            let block = match self.storage.get_block_by_number(&block_number)? {
                None => Err(format_err!("Block {} out of range", block_number)),
                Some(block) => Ok(block),
            }?;

            blocks.push(block);

            block_number -= U256::one();
        }

        let oldest_block = blocks.last().unwrap().header.number;

        let (mut base_fee_per_gas, mut gas_used_ratio): (Vec<U256>, Vec<f64>) = blocks
            .iter()
            .map(|block| {
                trace!("[fee_history] Processing block {}", block.header.number);
                let base_fee = block.header.base_fee;

                let gas_ratio = if block.header.gas_limit == U256::zero() {
                    f64::default() // empty block
                } else {
                    block.header.gas_used.as_u64() as f64 / block.header.gas_limit.as_u64() as f64
                };

                (base_fee, gas_ratio)
            })
            .unzip();

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
                    .map(|tx| tx.max_priority_fee_per_gas.as_u64() as f64)
                    .collect::<Vec<f64>>();
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
        let next_block_base_fee = match self
            .storage
            .get_block_by_number(&(first_block + U256::one()))?
        {
            None => {
                // get one block earlier (this should exist)
                let block = self
                    .storage
                    .get_block_by_number(&first_block)?
                    .ok_or_else(|| format_err!("Block {} out of range", first_block))?;
                self.calculate_base_fee(block.header.hash())?
            }
            Some(block) => self.calculate_base_fee(block.header.hash())?,
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

    /// Returns the 60th percentile priority fee for the last 20 blocks
    /// Ref: https://github.com/ethereum/go-ethereum/blob/c57b3436f4b8aae352cd69c3821879a11b5ee0fb/eth/ethconfig/config.go#L41
    /// TODO: these should be configurable by the user
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
                        priority_fees.push(t.max_priority_fee_per_gas.as_u64() as f64);
                    }
                }
            }
        }

        priority_fees.sort_by(|a, b| a.partial_cmp(b).expect("Invalid f64 value"));
        let mut data = Data::new(priority_fees);

        Ok(U256::from(data.percentile(60).ceil() as u64))
    }

    pub fn get_legacy_fee(&self) -> Result<U256> {
        let priority_fee = self.suggested_priority_fee()?;
        let base_fee = self
            .storage
            .get_latest_block()?
            .expect("Unable to get latest block")
            .header
            .base_fee;

        Ok(base_fee + priority_fee)
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
