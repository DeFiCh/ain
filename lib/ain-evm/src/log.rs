use crate::filters::LogsFilter;
use crate::receipt::Receipt;
use crate::storage::traits::LogStorage;
use crate::storage::Storage;
use ethereum::ReceiptV3;
use log::debug;
use primitive_types::{H160, H256, U256};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::Arc;

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct LogIndex {
    pub block_hash: H256,
    pub topics: Vec<H256>,
    pub data: Vec<u8>,
    pub log_index: U256,
    pub address: H160,
    pub removed: bool,
    pub transaction_hash: H256,
    pub transaction_index: usize,
}

pub struct LogService {
    storage: Arc<Storage>,
}

impl LogService {
    pub fn new(storage: Arc<Storage>) -> Self {
        Self { storage }
    }

    pub fn generate_logs_from_receipts(&self, receipts: &Vec<Receipt>, block_number: U256) {
        let mut logs_map: HashMap<H160, Vec<LogIndex>> = HashMap::new();
        let mut log_index = 0; // log index is a block level index
        for receipt in receipts {
            let logs = match &receipt.receipt {
                ReceiptV3::Legacy(r) => &r.logs,
                ReceiptV3::EIP2930(r) => &r.logs,
                ReceiptV3::EIP1559(r) => &r.logs,
            };

            for log in logs {
                let map = logs_map.entry(log.address).or_insert(Vec::new());

                map.push(LogIndex {
                    block_hash: receipt.block_hash,
                    topics: log.clone().topics,
                    data: log.clone().data,
                    log_index: U256::from(log_index),
                    address: log.clone().address,
                    removed: false, // hardcoded as no reorgs on DeFiChain
                    transaction_hash: receipt.tx_hash,
                    transaction_index: receipt.tx_index,
                });

                log_index += 1;
            }
        }

        logs_map
            .into_iter()
            .for_each(|(address, logs)| self.storage.put_logs(address, logs, block_number));
    }

    pub fn generate_logs_for_dst20_bridge(
        &self,
        contract_address: H160,
        address: H160,
        amount: U256,
        block_number: U256,
    ) {
        let log = vec![LogIndex {
            block_hash: Default::default(),
            topics: vec![H256::from(hex::encode("holaholahee").into_bytes())],
            data: format!("{amount:#x}").into_bytes(),
            log_index: Default::default(),
            address,
            removed: false,
            transaction_hash: Default::default(),
            transaction_index: 0,
        }];

        self.storage.put_logs(contract_address, log, block_number)
    }

    // get logs at a block height and filter for topics
    pub fn get_logs(
        &self,
        address: &Option<Vec<H160>>,
        topics: &Option<Vec<Option<H256>>>,
        block_number: U256,
    ) -> Vec<LogIndex> {
        debug!("Getting logs for block {:#x?}", block_number);
        let logs = self.storage.get_logs(&block_number).unwrap_or_default();

        let logs = match address {
            None => logs.into_iter().flat_map(|(_, log)| log).collect(),
            Some(addresses) => {
                // filter by addresses
                logs.into_iter()
                    .filter(|(address, _)| addresses.contains(address))
                    .flat_map(|(_, log)| log)
                    .collect()
            }
        };

        match topics {
            None => logs,
            Some(topics) => logs
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
        }
    }

    pub fn get_logs_from_filter(&self, filter: LogsFilter) -> Vec<LogIndex> {
        if filter.last_block_height >= filter.to_block {
            // not possible to have any new entries
            return Vec::new();
        }

        // get all logs that match filter from last_block_height to to_block
        let mut block_numbers = Vec::new();
        let mut block_number = filter.last_block_height;

        while block_number <= filter.to_block {
            debug!("Will query block {block_number}");
            block_numbers.push(block_number);
            block_number += U256::one();
        }

        block_numbers
            .into_iter()
            .flat_map(|block_number| {
                self.get_logs(&filter.address, &filter.topics, block_number)
                    .into_iter()
            })
            .collect()
    }
}
