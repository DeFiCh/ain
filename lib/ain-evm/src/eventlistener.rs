use crate::core::ExecutionStep;
use crate::opcode;
use evm::gasometer::tracing::{using as using_gas, EventListener as GasEventListener};
use evm::gasometer::Gasometer;
use evm::{Capture, ExitReason, Trap};
use evm_runtime::tracing::{using as using_runtime, Event, EventListener as RuntimeEventListener};
use evm_runtime::Config;
use log::debug;
use evm::Opcode;

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
                debug!("result : {:#?}", result);
                debug!("return_value : {:#?}", return_value);

                // handle calls
                match result.clone().err() {
                    None => {}
                    Some(e) => {
                        match e {
                            Capture::Exit(_) => {}
                            Capture::Trap(trap) => {
                                match trap {
                                    Opcode::DELEGATECALL => {
                                        debug!("need to DELETEGATECALL");
                                    }
                                    _ => {
                                        debug!("not deletegatecall")
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Event::SLoad {
                address,
                index,
                value,
            } => {}
            Event::SStore {
                address,
                index,
                value,
            } => {}
        }
    }
}
