use crate::core::ExecutionStep;
use crate::opcode;
use evm::gasometer::Gasometer;
use evm_runtime::tracing::{Event, EventListener as RuntimeEventListener};
use log::debug;

pub struct Listener<'a> {
    pub gasometer: Gasometer<'a>,
    pub trace: Vec<ExecutionStep>,
}

impl<'a> Listener<'a> {
    pub fn new(gasometer: Gasometer<'a>) -> Self {
        Self {
            gasometer,
            trace: vec![],
        }
    }
}

impl<'a> RuntimeEventListener for Listener<'a> {
    fn event(&mut self, event: Event<'_>) {
        debug!("event runtime : {:#?}", event);
        match event {
            Event::Step {
                opcode,
                position,
                stack,
                memory,
                ..
            } => {
                let gas_before = self.gasometer.gas();
                self.trace.push(ExecutionStep {
                    pc: *position.as_ref().unwrap(),
                    op: format!("{}", opcode::opcode_to_string(opcode)),
                    gas: gas_before,
                    gas_cost: gas_before - self.gasometer.gas(),
                    stack: stack.data().to_vec(),
                    memory: memory.data().to_vec(),
                });
            }
            Event::StepResult {
                result,
                return_value,
            } => {
                debug!("result : {:#?}", result);
                debug!("return_value : {:#?}", return_value);
            }
            Event::SLoad {
                address,
                index,
                value,
            } => {
                debug!("SLOAD, address: {address:#?}, index: {index:#?}, value: {value:#?}")
            }
            Event::SStore {
                address,
                index,
                value,
            } => {
                debug!("SSTORE, address: {address:#?}, index: {index:#?}, value: {value:#?}")
            }
        }
    }
}
