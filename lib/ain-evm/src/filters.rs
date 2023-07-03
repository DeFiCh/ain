use log::debug;
use primitive_types::{H160, H256, U256};
use std::collections::HashMap;
use std::sync::RwLock;

#[derive(Clone, Debug)]
pub enum Filter {
    Logs(LogsFilter),
    NewBlock(BlockFilter),
    NewPendingTransactions(PendingTransactionFilter),
}

#[derive(Clone, Debug)]
pub struct LogsFilter {
    pub id: usize,
    pub address: Option<Vec<H160>>,
    pub topics: Option<Vec<Option<H256>>>,
    pub from_block: U256,
    pub to_block: U256,
    // Last height that getFilterChanges was called at. We only need to store this since
    // logs are generated during block creation, so new logs will return all logs of the blocks
    // since the RPC was last called.
    pub last_block_height: U256,
}

impl TryInto<LogsFilter> for Filter {
    type Error = &'static str;

    fn try_into(self) -> Result<LogsFilter, Self::Error> {
        match self {
            Filter::Logs(logs_filter) => Ok(logs_filter),
            _ => Err("Conversion to LogsFilter failed. Filter is not of type Logs."),
        }
    }
}

#[derive(Clone, Debug)]
pub struct BlockFilter {
    pub id: usize,
    // List of block hashes since RPC was last called.
    pub block_hashes: Vec<H256>,
}

// TODO: can make this a trait
impl BlockFilter {
    pub fn get_entries(&mut self) -> Vec<H256> {
        self.block_hashes.drain(..).collect()
    }
}

#[derive(Clone, Debug)]
pub struct PendingTransactionFilter {
    pub id: usize,
    pub transaction_hashes: Vec<H256>,
}

// TODO: can make this a trait
impl PendingTransactionFilter {
    pub fn get_entries(&mut self) -> Vec<H256> {
        self.transaction_hashes.drain(..).collect()
    }
}

pub struct FilterHandler {
    pub filters: RwLock<HashMap<usize, Filter>>,
    pub filter_id: RwLock<usize>,
}

impl FilterHandler {
    pub fn new() -> Self {
        Self {
            filters: RwLock::new(HashMap::new()),
            filter_id: RwLock::new(0),
        }
    }

    pub fn create_logs_filter(
        &self,
        address: Option<Vec<H160>>,
        topics: Option<Vec<Option<H256>>>,
        from_block: U256,
        to_block: U256,
        current_block_height: U256,
    ) -> usize {
        let mut filter_id = self.filter_id.write().unwrap();
        *filter_id += 1;

        let mut filters = self.filters.write().unwrap();
        filters.insert(
            *filter_id,
            Filter::Logs(LogsFilter {
                address,
                id: *filter_id,
                topics,
                from_block,
                to_block,
                last_block_height: current_block_height,
            }),
        );

        return *filter_id;
    }

    pub fn create_block_filter(&self) -> usize {
        let mut filters = self.filters.write().unwrap();
        let mut filter_id = self.filter_id.write().unwrap();
        *filter_id += 1;

        let filter = Filter::NewBlock(BlockFilter {
            id: *filter_id,
            block_hashes: vec![],
        });

        filters.insert(*filter_id, filter);

        return *filter_id;
    }

    pub fn create_transaction_filter(&self) -> usize {
        let mut filters = self.filters.write().unwrap();
        let mut filter_id = self.filter_id.write().unwrap();
        *filter_id += 1;

        let filter = Filter::NewPendingTransactions(PendingTransactionFilter {
            id: *filter_id,
            transaction_hashes: vec![],
        });

        filters.insert(*filter_id, filter);

        return *filter_id;
    }

    pub fn get_filter(&self, filter_id: usize) -> Result<Filter, &str> {
        let filters = self.filters.read().unwrap();

        match filters.get(&filter_id) {
            Some(filter) => Ok(filter.clone()),
            None => Err("Unable to find filter"),
        }
    }

    pub fn update_last_block(&self, filter_id: usize, block_height: U256) -> Result<(), &str> {
        let mut filters = self.filters.write().unwrap();

        let Some(filter) = filters.get_mut(&filter_id) else {
            return Err("Filter not found");
        };

        match filter {
            Filter::Logs(f) => f.last_block_height = block_height,
            _ => {}
        }

        Ok(())
    }

    pub fn add_block_to_filters(&self, block_hash: H256) {
        let mut filters = self.filters.write().unwrap();
        debug!("Adding {:#x} to filters", block_hash);

        for item in filters.iter_mut() {
            let filter = item.1;
            match filter {
                Filter::NewBlock(filter) => {
                    debug!("Added block hash to {:#?}", filter.clone());
                    filter.block_hashes.push(block_hash);
                }
                _ => {}
            }
        }
    }

    pub fn add_tx_to_filters(&self, tx_hash: H256) {
        let mut filters = self.filters.write().unwrap();
        debug!("Adding {:#x} to filters", tx_hash);

        for item in filters.iter_mut() {
            let filter = item.1;
            match filter {
                Filter::NewPendingTransactions(filter) => {
                    debug!("Added tx hash to {:#?}", filter.clone());
                    filter.transaction_hashes.push(tx_hash);
                }
                _ => {}
            }
        }
    }

    pub fn get_entries_from_filter(&self, filter_id: usize) -> Result<Vec<H256>, &str> {
        let mut filters = self.filters.write().unwrap();

        let Some(filter) = filters.get_mut(&filter_id) else {
            return Err("Filter not found");
        };

        match filter {
            Filter::NewBlock(filter) => Ok(filter.get_entries()),
            Filter::NewPendingTransactions(filter) => Ok(filter.get_entries()),
            _ => Err("Filter is a log filter, must be transaction or block filter."),
        }
    }

    pub fn delete_filter(&self, filter_id: usize) -> bool {
        self.filters.write().unwrap().remove(&filter_id).is_some()
    }
}
