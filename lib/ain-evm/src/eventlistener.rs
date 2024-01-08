use ethereum_types::{H160, H256};
use std::collections::{HashMap, HashSet, VecDeque};

use evm::gasometer::tracing::{Event as GasEvent, EventListener as GasEventListener};
use evm_runtime::{
    tracing::{Event as RuntimeEvent, EventListener as RuntimeEventListener},
    Opcode,
};
use log::debug;

use crate::{core::ExecutionStep, opcode};

pub struct Listener {
    pub trace: Vec<ExecutionStep>,
    pub gas: VecDeque<u64>,
    pub gas_cost: VecDeque<u64>,
}

impl Listener {
    pub fn new(gas: VecDeque<u64>, gas_cost: VecDeque<u64>) -> Self {
        Self {
            trace: vec![],
            gas,
            gas_cost,
        }
    }
}

pub struct GasListener {
    pub gas: VecDeque<u64>,
    pub gas_cost: VecDeque<u64>,
    first_cost: bool,
}

impl GasListener {
    pub fn new() -> Self {
        Self {
            gas: VecDeque::new(),
            gas_cost: VecDeque::new(),
            first_cost: true,
        }
    }
}

pub struct StorageAccessListener {
    pub access_list: HashMap<H160, HashSet<H256>>,
}

impl StorageAccessListener {
    pub fn new() -> Self {
        Self {
            access_list: HashMap::new(),
        }
    }
}

impl GasEventListener for GasListener {
    fn event(&mut self, event: GasEvent) {
        debug!("gas event: {event:#?}");
        match event {
            GasEvent::RecordCost { cost, snapshot } => {
                if self.first_cost {
                    self.first_cost = false;
                    return;
                }

                self.gas_cost.push_back(cost);

                if let Some(snapshot) = snapshot {
                    self.gas.push_back(
                        snapshot.gas_limit - snapshot.used_gas - snapshot.memory_gas
                            + snapshot.refunded_gas as u64,
                    );
                } else {
                    panic!("No snapshot found!");
                }
            }
            GasEvent::RecordDynamicCost {
                gas_cost,
                memory_gas,
                gas_refund,
                snapshot,
                ..
            } => {
                if let Some(snapshot) = snapshot {
                    self.gas_cost
                        .push_back(gas_cost + memory_gas - snapshot.memory_gas + gas_refund as u64);
                    self.gas.push_back(
                        snapshot.gas_limit - snapshot.used_gas - snapshot.memory_gas
                            + snapshot.refunded_gas as u64,
                    );
                } else {
                    panic!("No snapshot found!");
                }
            }
            _ => {}
        }
    }
}

impl RuntimeEventListener for Listener {
    fn event(&mut self, event: RuntimeEvent<'_>) {
        debug!("event runtime : {:#?}", event);
        match event {
            RuntimeEvent::Step {
                opcode,
                position,
                stack,
                memory,
                ..
            } => {
                let mut gas_cost = self.gas_cost.pop_front().unwrap_or_default();

                match opcode {
                    // consume additional gas entries from call cost
                    Opcode::DELEGATECALL | Opcode::CALLCODE | Opcode::CALL => {
                        gas_cost += self.gas_cost.pop_front().unwrap_or_default();
                        gas_cost += self.gas_cost.pop_front().unwrap_or_default();
                    }
                    _ => {}
                }

                self.trace.push(ExecutionStep {
                    pc: *position.as_ref().unwrap(),
                    op: opcode::opcode_to_string(opcode),
                    gas: self.gas.pop_front().unwrap_or_default(),
                    gas_cost,
                    stack: stack.data().to_vec(),
                    memory: memory.data().to_vec(),
                });
            }
            RuntimeEvent::StepResult {
                result,
                return_value,
            } => {
                debug!("result : {:#?}", result);
                debug!("return_value : {:#?}", return_value);
            }
            RuntimeEvent::SLoad {
                address,
                index,
                value,
            } => {
                debug!("SLOAD, address: {address:#?}, index: {index:#?}, value: {value:#?}")
            }
            RuntimeEvent::SStore {
                address,
                index,
                value,
            } => {
                debug!("SSTORE, address: {address:#?}, index: {index:#?}, value: {value:#?}")
            }
        }
    }
}

impl RuntimeEventListener for StorageAccessListener {
    fn event(&mut self, event: RuntimeEvent<'_>) {
        debug!("event runtime : {:#?}", event);
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
}
