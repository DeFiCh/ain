// Copyright 2019-2022 PureStake Inc.
// This file is part of Moonbeam.

// Moonbeam is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// Moonbeam is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with Moonbeam.  If not, see <http://www.gnu.org/licenses/>.

extern crate alloc;

pub mod evm;
pub mod gasometer;
pub mod runtime;

pub use self::evm::EvmEvent;
pub use self::gasometer::GasometerEvent;
pub use self::runtime::RuntimeEvent;

use ethereum_types::{H160, U256};

/// Allow to configure which data of the Step event
/// we want to keep or discard. Not discarding the data requires cloning the data
/// in the runtime which have a significant cost for each step.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct StepEventFilter {
    pub enable_stack: bool,
    pub enable_memory: bool,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Event {
    Evm(EvmEvent),
    Gasometer(GasometerEvent),
    Runtime(RuntimeEvent),
    CallListNew(),
}

/// Main trait to proxy emitted messages.
/// Used 2 times :
/// - Inside the runtime to proxy the events through the host functions
/// - Inside the client to forward those events to the client listener.
pub trait Listener {
    fn event(&mut self, event: Event);

    /// Allow the runtime to know which data should be discarded and not cloned.
    /// WARNING: It is only called once when the runtime tracing is instantiated to avoid
    /// performing many ext calls.
    fn step_event_filter(&self) -> StepEventFilter;
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Context {
    /// Execution address.
    pub address: H160,
    /// Caller of the EVM.
    pub caller: H160,
    /// Apparent value of the EVM.
    pub apparent_value: U256,
}

impl From<evm_runtime::Context> for Context {
    fn from(i: evm_runtime::Context) -> Self {
        Self {
            address: i.address,
            caller: i.caller,
            apparent_value: i.apparent_value,
        }
    }
}
