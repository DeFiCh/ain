use std::{collections::HashMap, sync::Arc};

use anyhow::format_err;
use ethereum::ReceiptV3;
use ethereum_types::{H160, H256, U256};
use log::debug;
use serde::{Deserialize, Serialize};

use crate::{
    filters::LogsFilter,
    receipt::Receipt,
    storage::{traits::LogStorage, Storage},
    Result,
};

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct LogIndex {
    pub block_hash: H256,
    pub block_number: U256,
    pub topics: Vec<H256>,
    pub data: Vec<u8>,
    pub log_index: U256,
    pub address: H160,
    pub removed: bool,
    pub transaction_hash: H256,
    pub transaction_index: U256,
}

pub enum Notification {
    Block(H256),
    Transaction(H256),
}

pub struct LogService {
    storage: Arc<Storage>,
}

pub enum FilterType {
    GetFilterChanges,
    GetFilterLogs,
}

impl LogService {
    pub fn new(storage: Arc<Storage>) -> Self {
        Self { storage }
    }

    pub fn generate_logs_from_receipts(
        &self,
        receipts: &Vec<Receipt>,
        block_number: U256,
    ) -> Result<()> {
        let mut logs_map: HashMap<H160, Vec<LogIndex>> = HashMap::new();
        let mut log_index = 0_usize; // log index is a block level index
        for receipt in receipts {
            let logs = match &receipt.receipt {
                ReceiptV3::Legacy(r) | ReceiptV3::EIP2930(r) | ReceiptV3::EIP1559(r) => &r.logs,
            };

            for log in logs {
                let map = logs_map.entry(log.address).or_default();

                map.push(LogIndex {
                    block_hash: receipt.block_hash,
                    block_number,
                    topics: log.clone().topics,
                    data: log.clone().data,
                    log_index: U256::from(log_index),
                    address: log.clone().address,
                    removed: false, // hardcoded as no reorgs on DeFiChain
                    transaction_hash: receipt.tx_hash,
                    transaction_index: U256::from(receipt.tx_index),
                });

                log_index = log_index
                    .checked_add(1)
                    .ok_or_else(|| format_err!("log_index overflow"))?;
            }
        }

        for (address, logs) in logs_map {
            self.storage.put_logs(address, logs, block_number)?
        }
        Ok(())
    }

    // get logs at a block height and filter for topics
    pub fn get_logs(
        &self,
        address: &Option<Vec<H160>>,
        topics: &Option<Vec<Option<H256>>>,
        block_number: U256,
    ) -> Result<Vec<LogIndex>> {
        debug!("Getting logs for block {:#x?}", block_number);
        let logs = self.storage.get_logs(&block_number)?.unwrap_or_default();

        let filtered_logs: Vec<LogIndex> = match address {
            None => logs.into_iter().flat_map(|(_, log)| log).collect(),
            Some(addresses) => {
                // filter by addresses
                logs.into_iter()
                    .filter(|(address, _)| addresses.contains(address))
                    .flat_map(|(_, log)| log)
                    .collect()
            }
        };

        let logs = match topics {
            None => filtered_logs,
            Some(topics) => filtered_logs
                .into_iter()
                .filter(|log| {
                    topics
                        .iter() // for all topic filters
                        .zip(&log.topics) // construct tuple with corresponding log topics
                        .all(|(filter_item, log_item)| {
                            // check if topic filter at index matches log topic
                            filter_item.as_ref().map_or(true, |item| item == log_item)
                        })
                })
                .collect(),
        };
        Ok(logs)
    }

    pub fn get_logs_from_filter(
        &self,
        filter: &LogsFilter,
        filter_type: &FilterType,
    ) -> Result<Vec<LogIndex>> {
        let from_block_number = match filter_type {
            FilterType::GetFilterChanges => filter.last_block.unwrap_or(filter.from_block),
            FilterType::GetFilterLogs => filter.from_block,
        };

        if from_block_number >= filter.to_block {
            // not possible to have any new entries
            return Ok(Vec::new());
        }

        // get all logs that match filter from block_number to to_block
        let mut block_numbers = Vec::new();
        let mut from_block_number = from_block_number;

        while from_block_number <= filter.to_block {
            debug!("Will query block {from_block_number}");
            block_numbers.push(from_block_number);
            from_block_number = from_block_number
                .checked_add(U256::one())
                .ok_or_else(|| format_err!("from_block_number overflow"))?;
        }

        let logs = block_numbers
            .into_iter()
            .map(|block_number| self.get_logs(&filter.address, &filter.topics, block_number))
            .collect::<Result<Vec<_>>>()?
            .into_iter()
            .flatten()
            .collect();

        Ok(logs)
    }
}
