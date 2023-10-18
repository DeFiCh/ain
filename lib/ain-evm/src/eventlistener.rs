use crate::core::ExecutionStep;
use crate::opcode;
use evm_runtime::tracing::{Event as RuntimeEvent, EventListener as RuntimeEventListener};
use evm::gasometer::tracing::{Event as GasEvent, EventListener as GasEventListener};

use log::debug;

pub struct Listener {
    pub trace: Vec<ExecutionStep>,
}

impl Listener {
    pub fn new() -> Self {
        Self {
            trace: vec![],
        }
    }
}

impl GasEventListener for Listener {
    fn event(&mut self, event: GasEvent) {
        debug!("gas event: {event:#?}");
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
                self.trace.push(ExecutionStep {
                    pc: *position.as_ref().unwrap(),
                    op: format!("{}", opcode::opcode_to_string(opcode)),
                    gas: 0,
                    gas_cost: 0,
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