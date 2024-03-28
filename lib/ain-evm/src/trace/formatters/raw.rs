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

use std::{cell::RefCell, rc::Rc};

use crate::trace::{listeners::raw::Listener, types::single::TransactionTrace};
use ethereum_types::U256;

pub struct Formatter;

impl super::ResponseFormatter for Formatter {
    type Listener = Listener;
    type Response = TransactionTrace;

    fn format(listener: Rc<RefCell<Listener>>, system_tx: bool) -> Option<TransactionTrace> {
        let l = listener.borrow();
        if l.remaining_memory_usage.is_none() {
            None
        } else {
            Some(TransactionTrace::Raw {
                struct_logs: l.struct_logs.clone(),
                failed: l.exec_flag,
                gas: if system_tx {
                    U256::zero()
                } else {
                    l.final_gas.into()
                },
                return_value: l.return_value.clone(),
            })
        }
    }
}
