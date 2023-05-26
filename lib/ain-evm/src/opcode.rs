use std::fmt;
use std::fmt::Write;
use evm::Opcode;
use evm::gasometer;
use evm::gasometer::GasCost;

pub fn get_cost(opcode: Opcode) -> Option<u64> {
    if opcode == Opcode::MSTORE { // TODO: fallback to dynamic_opcode_cost
        return Some(3);
    }

    gasometer::static_opcode_cost(opcode)
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
