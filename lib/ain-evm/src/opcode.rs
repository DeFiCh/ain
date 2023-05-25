use std::fmt;
use std::fmt::Write;
use evm::Opcode;

pub struct OpCode(pub Opcode);

impl fmt::Display for OpCode {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            OpCode(Opcode::STOP) => fmt.write_str("STOP"),
            OpCode(Opcode::MSTORE) => fmt.write_str("MSTORE"),
            OpCode(Opcode::CALLVALUE) => fmt.write_str("CALLVALUE"),
            OpCode(Opcode::DUP1) => fmt.write_str("DUP1"),
            OpCode(Opcode::PUSH1) => fmt.write_str("PUSH1"),
            OpCode(Opcode::PUSH6) => fmt.write_str("PUSH6"),
            _ => fmt.write_str("UNKNOWN")
        }
    }
}

impl OpCode {
    pub fn gas_cost(&self) -> u64 {
        match *self {
            OpCode(Opcode::STOP) => 0,
            OpCode(Opcode::MSTORE) => 3,
            OpCode(Opcode::CALLVALUE) => 2,
            OpCode(Opcode::DUP1) => 3,
            OpCode(Opcode::PUSH1) | OpCode(Opcode::PUSH6) => 3,
            _ => 0,
        }
    }
}