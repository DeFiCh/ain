use crate::receipt::Receipt;
use crate::storage::traits::LogStorage;
use crate::storage::Storage;
use ethereum::ReceiptV3;
use log::debug;
use primitive_types::{H160, H256, U256};
use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet};
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

pub struct LogHandler {
    storage: Arc<Storage>,
}

impl LogHandler {
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

    // get logs at a block height and filter for topics
    pub fn get_logs(
        &self,
        address: Option<Vec<H160>>,
        topics: Option<Vec<H256>>,
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
                    .into_iter()
                    .flat_map(|(_, log)| log)
                    .collect()
            }
        };

        match topics {
            None => logs,
            Some(topics) => logs
                .into_iter()
                .filter(|log| {
                    let set: HashSet<_> = log.topics.iter().copied().collect();
                    topics.iter().any(|item| set.contains(item))
                })
                .collect(),
        }
    }
}
