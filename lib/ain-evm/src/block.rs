use ethereum::{BlockAny, TransactionAny};
use keccak_hash::H256;
use log::debug;
use primitive_types::U256;

use statrs::statistics::{Data, OrderStatistics};
use std::cmp::{max, Ordering};
use std::sync::Arc;

use crate::storage::{traits::BlockStorage, Storage};

pub struct BlockHandler {
    storage: Arc<Storage>,
}

pub struct FeeHistoryData {
    pub oldest_block: H256,
    pub base_fee_per_gas: Vec<U256>,
    pub gas_used_ratio: Vec<f64>,
    pub reward: Option<Vec<Vec<U256>>>,
}

pub const INITIAL_BASE_FEE: U256 = U256([10_000_000_000, 0, 0, 0]); // wei

impl BlockHandler {
    pub fn new(storage: Arc<Storage>) -> Self {
        Self { storage }
    }

    pub fn get_latest_block_hash_and_number(&self) -> Option<(H256, U256)> {
        self.storage
            .get_latest_block()
            .map(|latest_block| (latest_block.header.hash(), latest_block.header.number))
    }

    pub fn get_latest_state_root(&self) -> H256 {
        self.storage
            .get_latest_block()
            .map(|block| block.header.state_root)
            .unwrap_or_default()
    }

    pub fn connect_block(&self, block: BlockAny, base_fee: U256) {
        self.storage.put_latest_block(Some(&block));
        self.storage.put_block(&block);
        self.storage.set_base_fee(block.header.hash(), base_fee);
    }

    pub fn calculate_base_fee(&self, parent_hash: H256) -> U256 {
        // constants
        let base_fee_max_change_denominator = U256::from(8);
        let elasticity_multiplier = U256::from(2);

        // first block has 1 gwei base fee
        if parent_hash == H256::zero() {
            return INITIAL_BASE_FEE;
        }

        // get parent gas usage,
        // https://eips.ethereum.org/EIPS/eip-1559#:~:text=fee%20is%20correct-,if%20INITIAL_FORK_BLOCK_NUMBER%20%3D%3D%20block.number%3A,-expected_base_fee_per_gas%20%3D%20INITIAL_BASE_FEE
        let parent_block = self
            .storage
            .get_block_by_hash(&parent_hash)
            .expect("Parent block not found");
        let parent_base_fee = self
            .storage
            .get_base_fee(&parent_block.header.hash())
            .expect("Parent base fee not found");
        let parent_gas_used = parent_block.header.gas_used.as_u64();
        let parent_gas_target =
            parent_block.header.gas_limit.as_u64() / elasticity_multiplier.as_u64();

        match parent_gas_used.cmp(&parent_gas_target) {
            Ordering::Less => parent_base_fee,
            Ordering::Equal => {
                let gas_used_delta = parent_gas_used - parent_gas_target;
                let base_fee_per_gas_delta = max(
                    parent_base_fee * gas_used_delta
                        / parent_gas_target
                        / base_fee_max_change_denominator,
                    U256::one(),
                );

                max(parent_base_fee + base_fee_per_gas_delta, INITIAL_BASE_FEE)
            }
            Ordering::Greater => {
                let gas_used_delta = parent_gas_target - parent_gas_used;
                let base_fee_per_gas_delta = parent_base_fee * gas_used_delta
                    / parent_gas_target
                    / base_fee_max_change_denominator;

                max(parent_base_fee - base_fee_per_gas_delta, INITIAL_BASE_FEE)
            }
        }
    }

    pub fn fee_history(
        &self,
        block_count: usize,
        first_block: U256,
        priority_fee_percentile: Vec<usize>,
    ) -> FeeHistoryData {
        let mut blocks = Vec::with_capacity(block_count);
        let mut block_number = first_block;

        for _ in 0..=block_count {
            blocks.push(
                self.storage
                    .get_block_by_number(&block_number)
                    .unwrap_or_else(|| panic!("Block {} out of range", block_number)),
            );

            block_number -= U256::one();
        }

        let oldest_block = blocks.last().unwrap().header.hash();

        let (mut base_fee_per_gas, mut gas_used_ratio): (Vec<U256>, Vec<f64>) = blocks
            .iter()
            .map(|block| {
                debug!("Processing block {}", block.header.number);
                let base_fee = self
                    .storage
                    .get_base_fee(&block.header.hash())
                    .unwrap_or_else(|| panic!("No base fee for block {}", block.header.number));

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

                for pct in priority_fee_percentile.iter() {
                    block_rewards.push(U256::from(data.percentile(*pct).ceil() as u64));
                }

                reward.push(block_rewards);
            }

            reward.reverse();
            Some(reward)
        };

        base_fee_per_gas.reverse();
        gas_used_ratio.reverse();

        FeeHistoryData {
            oldest_block,
            base_fee_per_gas,
            gas_used_ratio,
            reward,
        }
    }

    /// Returns the 60th percentile priority fee for the last 20 blocks
    /// Ref: https://github.com/ethereum/go-ethereum/blob/c57b3436f4b8aae352cd69c3821879a11b5ee0fb/eth/ethconfig/config.go#L41
    /// TODO: these should be configurable by the user
    pub fn suggested_priority_fee(&self) -> U256 {
        let mut blocks = Vec::with_capacity(20);
        let block = self
            .storage
            .get_latest_block()
            .expect("Unable to find latest block");
        blocks.push(block.clone());
        let mut parent_hash = block.header.parent_hash;

        while blocks.len() <= blocks.capacity() {
            match self.storage.get_block_by_hash(&parent_hash) {
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

        U256::from(data.percentile(60).ceil() as u64)
    }

    pub fn get_legacy_fee(&self) -> U256 {
        let priority_fee = self.suggested_priority_fee();
        let latest_block_hash = self
            .storage
            .get_latest_block()
            .expect("Unable to get latest block")
            .header
            .hash();
        let base_fee = self.storage.get_base_fee(&latest_block_hash).unwrap();

        base_fee + priority_fee
    }
}
