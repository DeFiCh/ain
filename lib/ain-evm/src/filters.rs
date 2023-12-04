use std::{
    cmp::min,
    collections::BTreeSet,
    num::NonZeroUsize,
    sync::{Arc, RwLock},
};

use anyhow::format_err;
use ethereum_types::{H160, H256, U256};
use log::debug;
use lru::LruCache;

use crate::{
    log::LogIndex,
    storage::{
        traits::{BlockStorage, LogStorage},
        Storage,
    },
    EVMError, Result,
};

// The maximum number of topic criteria allowed, vm.LOG4 - vm.LOG0
const MAX_TOPICS: usize = 4;

// The default LRU cache size
const FILTER_LRU_CACHE_DEFAULT_SIZE: usize = 5000;

// The maximum block range limit
const BLOCK_RANGE_LIMIT: U256 = U256([2000, 0, 0, 0]);

// The maximum number of logs returned in a single response
const RESPONSE_LOG_LIMIT: usize = 10_000;

#[derive(Clone, Debug)]
pub enum FilterResults {
    Logs(Vec<LogIndex>),
    Blocks(Vec<H256>),
    Transactions(Vec<H256>),
}

// Filter is a helper struct that holds meta information over the filter type. It
// represents a request to create a new filter, and the polling information.
#[derive(Clone, Debug)]
pub enum Filter {
    // Logs filter holds the filter criteria and the last block polled.
    Logs(LogsFilter),
    // Blocks filter holds the last block number polled.
    Blocks(U256),
    // Transactions filter holds the last set of tx hashes polled.
    Transactions(BTreeSet<H256>),
}

// FilterCriteria encapsulates the arguments to the filter query, containing options
// for contract log filtering.
// Ref: https://github.com/ethereum/go-ethereum/blob/master/interfaces.go#L154-L172
#[derive(Clone, Debug, Default)]
pub struct FilterCriteria {
    // Used by eth_getLogs, return logs only from block with this hash
    pub block_hash: Option<H256>,
    // Start of the queried range, nil represents genesis block
    pub from_block: Option<U256>,
    // End of the range, nil represents latest block
    pub to_block: Option<U256>,
    // Restricts matches to events created by specific contracts
    pub addresses: Option<Vec<H160>>,
    // The topic list restricts matches to particular event topics. Each event has a list
    // of topics. Topics matches a prefix of that list. An empty element slice matches any
    // topic. Non-empty elements represent an alternative that matches any of the contained
    // topics.
    // Examples:
    // {} or nil          matches any topic list
    // {{A}}              matches topic A in first position
    // {{}, {B}}          matches any topic in first position, B in second position
    // {{A}, {B}}         matches topic A in first position, B in second position
    // {{A, B}}, {C, D}}  matches topic (A OR B) in first position, (C OR D) in second position
    pub topics: Option<Vec<Vec<H256>>>,
}

impl FilterCriteria {
    pub fn verify_criteria(&mut self, latest: U256) -> Result<()> {
        if self.block_hash.is_some() && (self.from_block.is_some() || self.to_block.is_some()) {
            return Err(FilterError::InvalidFilter.into());
        }
        if self.block_hash.is_none() {
            let from_block = if let Some(from) = self.from_block {
                from
            } else {
                // Default to genesis block if input not specified
                U256::zero()
            };
            let to_block = if let Some(to) = self.to_block {
                to
            } else {
                // Default to latest block (inclusive of finality count) if input not specifed
                latest
            };
            if from_block > to_block {
                return Err(FilterError::InvalidBlockRange.into());
            }
            if to_block - from_block > BLOCK_RANGE_LIMIT {
                return Err(FilterError::ExceedBlockRange.into());
            }
            if let Some(t) = &self.topics {
                if t.len() > MAX_TOPICS {
                    return Err(FilterError::ExceedMaxTopics.into());
                }
            }
            self.from_block = Some(from_block);
            self.to_block = Some(to_block);
        }
        Ok(())
    }
}

#[derive(Clone, Debug)]
pub struct LogsFilter {
    pub criteria: FilterCriteria,
    // Last height that getFilterChanges was called at. We only need to store this since
    // logs are generated during block creation, so new logs will return all logs of the blocks
    // since the RPC was last called.
    pub last_block: Option<U256>,
}

pub enum FilterError {
    InvalidFilter,
    FilterNotFound,
    InvalidBlockRange,
    ExceedBlockRange,
    ExceedMaxTopics,
    BlockNotFound,
}

impl From<FilterError> for EVMError {
    fn from(e: FilterError) -> Self {
        match e {
            FilterError::InvalidFilter => format_err!("invalid filter").into(),
            FilterError::FilterNotFound => format_err!("filter not found").into(),
            FilterError::InvalidBlockRange => {
                format_err!("fromBlock is greater than toBlock").into()
            }
            FilterError::ExceedBlockRange => format_err!("block range exceed max limit").into(),
            FilterError::ExceedMaxTopics => format_err!("exceed max topics").into(),
            FilterError::BlockNotFound => format_err!("header not found").into(),
        }
    }
}

pub struct FilterSystem {
    id: usize,
    cache: LruCache<usize, Filter>,
}

impl FilterSystem {
    pub fn create_log_filter(&mut self, criteria: FilterCriteria) -> usize {
        self.id = self.id.wrapping_add(1);
        self.cache.put(
            self.id,
            Filter::Logs(LogsFilter {
                criteria,
                last_block: None,
            }),
        );
        self.id
    }

    pub fn create_block_filter(&mut self, block_number: U256) -> usize {
        self.id = self.id.wrapping_add(1);
        self.cache.put(self.id, Filter::Blocks(block_number));
        self.id
    }

    pub fn create_tx_filter(&mut self) -> usize {
        self.id = self.id.wrapping_add(1);
        self.cache
            .put(self.id, Filter::Transactions(BTreeSet::new()));
        self.id
    }

    pub fn get_filter(&mut self, filter_id: usize) -> Result<Filter> {
        if let Some(entry) = self.cache.get(&filter_id) {
            Ok(entry.clone())
        } else {
            Err(FilterError::FilterNotFound.into())
        }
    }

    pub fn delete_filter(&mut self, filter_id: usize) -> bool {
        self.cache.pop(&filter_id).is_some()
    }

    pub fn update_last_block(&mut self, filter_id: usize, last_block: U256) -> Result<()> {
        if let Some(entry) = self.cache.get_mut(&filter_id) {
            match entry {
                Filter::Logs(f) => f.last_block = Some(last_block),
                Filter::Blocks(f) => *f = last_block,
                Filter::Transactions(_) => return Err(FilterError::InvalidFilter.into()),
            };
            Ok(())
        } else {
            Err(FilterError::FilterNotFound.into())
        }
    }
}

pub struct FilterService {
    storage: Arc<Storage>,
    system: RwLock<FilterSystem>,
}

// Filter system methods
impl FilterService {
    pub fn new(storage: Arc<Storage>) -> Self {
        Self {
            storage,
            system: RwLock::new(FilterSystem {
                id: 0,
                cache: LruCache::new(NonZeroUsize::new(FILTER_LRU_CACHE_DEFAULT_SIZE).unwrap()),
            }),
        }
    }

    pub fn create_log_filter(&self, criteria: FilterCriteria) -> usize {
        let mut system: std::sync::RwLockWriteGuard<'_, FilterSystem> =
            self.system.write().unwrap();
        system.create_log_filter(criteria)
    }

    pub fn create_block_filter(&self) -> Result<usize> {
        let mut system = self.system.write().unwrap();

        let block_number = if let Some(block) = self.storage.get_latest_block()? {
            block.header.number
        } else {
            U256::zero()
        };
        let id = system.create_block_filter(block_number);
        Ok(id)
    }

    pub fn create_tx_filter(&self) -> usize {
        let mut system = self.system.write().unwrap();
        system.create_tx_filter()
    }

    pub fn delete_filter(&self, filter_id: usize) -> bool {
        let mut system = self.system.write().unwrap();
        system.delete_filter(filter_id)
    }
}

// Filter methods
impl FilterService {
    pub fn get_block_logs(
        &self,
        criteria: &FilterCriteria,
        block_number: U256,
    ) -> Result<Vec<LogIndex>> {
        debug!("Getting logs for block {:#x?}", block_number);
        // Possible for blocks to contain no logs, default to empty map.
        let logs = self.storage.get_logs(&block_number)?.unwrap_or_default();

        // Filter by addresses
        let logs: Vec<LogIndex> = match &criteria.addresses {
            None => logs.into_iter().flat_map(|(_, log)| log).collect(),
            Some(addresses) => logs
                .into_iter()
                .filter(|(address, _)| addresses.contains(address))
                .flat_map(|(_, log)| log)
                .collect(),
        };

        // Filter by topics
        let logs = match &criteria.topics {
            None => logs,
            Some(topics) => logs
                .into_iter()
                .filter(|log| {
                    log.topics
                        .clone()
                        .into_iter()
                        .enumerate()
                        .all(|(idx, log_item)| {
                            if idx < topics.len() {
                                let topic = &topics[idx];
                                if topic.is_empty() {
                                    true
                                } else {
                                    topic.contains(&log_item)
                                }
                            } else {
                                true
                            }
                        })
                })
                .collect(),
        };
        Ok(logs)
    }

    pub fn get_logs_from_filter(&self, criteria: &FilterCriteria) -> Result<Vec<LogIndex>> {
        if let Some(block_hash) = criteria.block_hash {
            let block_number = if let Some(block) = self.storage.get_block_by_hash(&block_hash)? {
                block.header.number
            } else {
                return Err(FilterError::BlockNotFound.into());
            };
            self.get_block_logs(criteria, block_number)
        } else {
            let Some(from_block) = criteria.from_block else {
                return Err(FilterError::InvalidFilter.into());
            };
            let Some(to_block) = criteria.to_block else {
                return Err(FilterError::InvalidFilter.into());
            };

            let mut logs = vec![];
            let mut curr = from_block;
            while curr <= to_block && logs.len() < RESPONSE_LOG_LIMIT {
                let mut block_logs = self.get_block_logs(criteria, curr)?;
                logs.append(&mut block_logs);
                curr += U256::one();
            }
            Ok(logs)
        }
    }

    pub fn get_filter_logs_from_entry(
        &self,
        entry: LogsFilter,
        filter_change: bool,
        curr_block: U256,
    ) -> Result<Vec<LogIndex>> {
        let mut criteria = entry.criteria.clone();
        if filter_change {
            if let Some(last_block) = entry.last_block {
                criteria.from_block = Some(last_block + U256::one());
            }
        }
        if let Some(to_block) = criteria.to_block {
            criteria.to_block = Some(min(curr_block, to_block));
        }
        self.get_logs_from_filter(&criteria)
    }

    pub fn get_filter_blocks_from_entry(
        &self,
        last_block: U256,
        target_block: U256,
    ) -> Result<Vec<H256>> {
        let mut out = vec![];
        let mut curr = last_block + U256::one();
        while curr <= target_block && out.len() < RESPONSE_LOG_LIMIT {
            let hash = if let Some(block) = self.storage.get_block_by_number(&curr)? {
                block.header.hash()
            } else {
                return Err(FilterError::BlockNotFound.into());
            };
            out.push(hash);
            curr += U256::one();
        }
        Ok(out)
    }

    pub fn get_filter_logs_from_id(
        &self,
        filter_id: usize,
        curr_block: U256,
    ) -> Result<Vec<LogIndex>> {
        let mut system = self.system.write().unwrap();
        let entry = system.get_filter(filter_id)?;
        if let Filter::Logs(entry) = entry {
            self.get_filter_logs_from_entry(entry, false, curr_block)
        } else {
            Err(FilterError::InvalidFilter.into())
        }
    }

    pub fn get_filter_changes_from_id(
        &self,
        filter_id: usize,
        curr_block: U256,
    ) -> Result<FilterResults> {
        let mut system = self.system.write().unwrap();
        let entry = system.get_filter(filter_id)?;
        match entry {
            Filter::Logs(entry) => {
                let out = self.get_filter_logs_from_entry(entry, true, curr_block)?;
                system.update_last_block(filter_id, curr_block)?;
                Ok(FilterResults::Logs(out))
            }
            Filter::Blocks(last_block) => {
                let out = self.get_filter_blocks_from_entry(last_block, curr_block)?;
                system.update_last_block(filter_id, curr_block)?;
                Ok(FilterResults::Blocks(out))
            }
            Filter::Transactions(_) => Ok(FilterResults::Transactions(vec![])),
        }
    }
}
