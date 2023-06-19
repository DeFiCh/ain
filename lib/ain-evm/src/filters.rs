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
    pub topics: Option<Vec<H256>>,
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
}

impl FilterHandler {
    pub fn new() -> Self {
        Self {
            filters: RwLock::new(HashMap::new()),
        }
    }

    pub fn create_logs_filter(
        &self,
        address: Option<Vec<H160>>,
        topics: Option<Vec<H256>>,
        from_block: U256,
        to_block: U256,
        current_block_height: U256,
    ) -> usize {
        let mut filters = self.filters.write().unwrap();
        let filter_id = filters.len();

        filters.insert(
            filter_id,
            Filter::Logs(LogsFilter {
                address,
                id: filter_id,
                topics,
                from_block,
                to_block,
                last_block_height: current_block_height,
            }),
        );

        return filter_id;
    }

    pub fn get_filter(&self, filter_id: usize) -> Result<Filter, &str> {
        let filters = self.filters.read().unwrap();

        match filters.get(&filter_id) {
            Some(filter) => Ok(filter.clone()),
            None => Err("Unable to find filter"),
        }
    }

    pub fn update_last_block(&self, filter_id: usize, block_height: U256) {
        let mut filters = self.filters.write().unwrap();

        let filter = filters.get_mut(&filter_id).unwrap();

        match filter {
            Filter::Logs(f) => f.last_block_height = block_height,
            _ => {}
        }
    }
}
