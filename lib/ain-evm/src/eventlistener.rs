use evm::gasometer::Gasometer;
use crate::core::ExecutionStep;
use crate::opcode;
use evm::gasometer::tracing::{using as using_gas, EventListener as GasEventListener};
use evm_runtime::Config;
use evm_runtime::tracing::{using as using_runtime, EventListener as RuntimeEventListener, Event};

pub struct Listener {
    pub gasometer: Gasometer<'static>,
    pub trace: Vec<ExecutionStep>,
}

impl Listener {
    pub fn new(gasometer: Gasometer<'static>) -> Self {
        Self {
            gasometer,
            trace: vec![],
        }
    }
}

impl RuntimeEventListener for Listener {
    fn event(&mut self, event: Event<'_>) {
        println!("event runtime : {:#?}", event);
        match event {
            Event::Step {
                context,
                opcode,
                position,
                stack,
                memory,
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
                println!("result : {:#?}", result);
                println!("return_value : {:#?}", return_value);
            }
            Event::SLoad {
                address,
                index,
                value,
            } => todo!(),
            Event::SStore {
                address,
                index,
                value,
            } => todo!(),
        }
    }
}