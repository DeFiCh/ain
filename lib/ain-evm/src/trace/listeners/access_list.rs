use std::collections::{HashMap, HashSet};

use crate::trace::tracing::{Event, Listener as ListenerT, RuntimeEvent, StepEventFilter};
use ethereum::{AccessList, AccessListItem};
use ethereum_types::{H160, H256};

#[derive(Default)]
pub struct Listener {
    access_list: HashMap<H160, HashSet<H256>>,
}

impl Listener {
    pub fn runtime_event(&mut self, event: RuntimeEvent) {
        match event {
            RuntimeEvent::SLoad { address, index, .. } => {
                self.access_list.entry(address).or_default().insert(index);
            }
            RuntimeEvent::SStore { address, index, .. } => {
                self.access_list.entry(address).or_default().insert(index);
            }
            _ => {}
        }
    }

    pub fn finish_transaction(&self) -> AccessList {
        self.access_list
            .clone()
            .into_iter()
            .map(|(address, storage_keys)| AccessListItem {
                address,
                storage_keys: Vec::from_iter(storage_keys),
            })
            .collect()
    }
}

impl ListenerT for Listener {
    fn event(&mut self, event: Event) {
        if let Event::Runtime(runtime_event) = event {
            self.runtime_event(runtime_event);
        }
    }

    fn step_event_filter(&self) -> StepEventFilter {
        StepEventFilter {
            enable_memory: false,
            enable_stack: false,
        }
    }
}
