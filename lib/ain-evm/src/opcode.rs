<<<<<<< HEAD
use std::fmt;
use std::fmt::Write;
use evm::Opcode;
use evm::gasometer;
use evm::gasometer::GasCost;

pub fn get_cost(opcode: Opcode) -> Option<u64> {
    if opcode == Opcode::MSTORE { // TODO: fallback to dynamic_opcode_cost
        return Some(3);
=======
use evm::gasometer::Gasometer;
use evm::Opcode;
use evm::{gasometer, Config, Handler, Stack};
use primitive_types::H160;

pub fn record_cost<'a, H: Handler>(
    opcode: Opcode,
    gasometer: &mut Gasometer,
    to: Option<H160>,
    stack: &Stack,
    executor: &mut H,
    config: &Config,
) {
    match gasometer::static_opcode_cost(opcode) {
        Some(cost) => gasometer.record_cost(cost).expect("Out of Gas"),
        None => {
            let (gas_cost, _, memory) = gasometer::dynamic_opcode_cost(
                to.unwrap_or_default(),
                opcode,
                stack,
                false,
                &config,
                executor,
            )
            .expect("Error getting dynamic cost");
            gasometer
                .record_dynamic_cost(gas_cost, memory)
                .expect("Error recording dynamic gas cost");
        }
>>>>>>> c5f28a1c8 (Abstract opcode cost getters)
    }
}

pub fn opcode_to_string(opcode: Opcode) -> String {
    let x = match opcode {
        Opcode::STOP => "STOP",
        Opcode::MSTORE => "MSTORE",
        Opcode::CALLVALUE => "CALLVALUE",
        Opcode::DUP1 => "DUP1",
        Opcode::PUSH1 => "PUSH1",
        Opcode::PUSH6 => "PUSH6",
        _ => "UNKNOWN"
    };

    return String::from(x);
}
