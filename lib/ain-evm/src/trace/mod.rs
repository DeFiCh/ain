/// EVM tracing module.
///
/// Contains tracing of the EVM opcode execution used by Dapp develops and
/// indexers to access the EVM callstack (nteranl transactions) and get
/// granular view on their transactions.
pub mod formatters;
pub mod listeners;
pub mod service;
pub mod tracing;
pub mod types;

use formatters::call_tracer::{CallTracerCall, CallTracerInner};
use tracing::{Event, EvmEvent, GasometerEvent, Listener, RuntimeEvent, StepEventFilter};
use types::single::{Call, TraceType, TracerInput, TransactionTrace};

use crate::Result;
use anyhow::format_err;
use ethereum_types::{H160, U256};
use evm::{
    gasometer::tracing::{using as gasometer_using, EventListener as GasometerListener},
    tracing::{using as evm_using, EventListener as EvmListener},
};
use evm_runtime::tracing::{using as runtime_using, EventListener as RuntimeListener};
use std::{cell::RefCell, rc::Rc};

struct ListenerProxy<T>(pub Rc<RefCell<T>>);
impl<T: GasometerListener> GasometerListener for ListenerProxy<T> {
    fn event(&mut self, event: evm::gasometer::tracing::Event) {
        self.0.borrow_mut().event(event);
    }
}

impl<T: RuntimeListener> RuntimeListener for ListenerProxy<T> {
    fn event(&mut self, event: evm_runtime::tracing::Event) {
        self.0.borrow_mut().event(event);
    }
}

impl<T: EvmListener> EvmListener for ListenerProxy<T> {
    fn event(&mut self, event: evm::tracing::Event) {
        self.0.borrow_mut().event(event);
    }
}

pub struct EvmTracer<T: Listener + 'static> {
    listener: Rc<RefCell<T>>,
    step_event_filter: StepEventFilter,
}

impl<T: Listener + 'static> EvmTracer<T> {
    pub fn new(listener: Rc<RefCell<T>>) -> Self {
        let step_event_filter = listener.borrow_mut().step_event_filter();

        Self {
            listener,
            step_event_filter,
        }
    }

    /// Setup event listeners and execute provided closure.
    ///
    /// Consume the tracer and return it alongside the return value of
    /// the closure.
    pub fn trace<R, F: FnOnce() -> R>(self, f: F) -> R {
        let wrapped = Rc::new(RefCell::new(self));

        let mut gasometer = ListenerProxy(Rc::clone(&wrapped));
        let mut runtime = ListenerProxy(Rc::clone(&wrapped));
        let mut evm = ListenerProxy(Rc::clone(&wrapped));

        // Each line wraps the previous `f` into a `using` call.
        // Listening to new events results in adding one new line.
        // Order is irrelevant when registering listeners.
        let f = || runtime_using(&mut runtime, f);
        let f = || gasometer_using(&mut gasometer, f);
        let f = || evm_using(&mut evm, f);
        f()
    }
}

impl<T: Listener + 'static> EvmListener for EvmTracer<T> {
    /// Proxies `evm::tracing::Event` to the host.
    fn event(&mut self, event: evm::tracing::Event) {
        let event: EvmEvent = event.into();
        self.listener.borrow_mut().event(Event::Evm(event));
    }
}

impl<T: Listener + 'static> GasometerListener for EvmTracer<T> {
    /// Proxies `evm_gasometer::tracing::Event` to the host.
    fn event(&mut self, event: evm::gasometer::tracing::Event) {
        let event: GasometerEvent = event.into();
        self.listener.borrow_mut().event(Event::Gasometer(event));
    }
}

impl<T: Listener + 'static> RuntimeListener for EvmTracer<T> {
    /// Proxies `evm_runtime::tracing::Event` to the host.
    fn event(&mut self, event: evm_runtime::tracing::Event) {
        let event = RuntimeEvent::from_evm_event(event, self.step_event_filter);
        self.listener.borrow_mut().event(Event::Runtime(event));
    }
}

pub fn get_dst20_system_tx_trace(
    address: H160,
    tracer_params: (TracerInput, TraceType),
) -> Result<TransactionTrace> {
    // TODO: running trace on DST20 deployment/update txs will be buggy at the moment as these custom
    // system txs are never executed on the VM. More thought has to be placed into how we can do accurate
    // traces on the custom system txs related to DST20 tokens, since these txs are state injections on
    // execution. For now, empty execution step with a successful execution trace is returned.
    match tracer_params.1 {
        TraceType::Raw { .. } => Ok(TransactionTrace::Raw {
            gas: U256::zero(),
            failed: false,
            return_value: vec![],
            struct_logs: vec![],
        }),
        TraceType::CallList => match tracer_params.0 {
            TracerInput::Blockscout => Ok(TransactionTrace::CallList(vec![])),
            TracerInput::CallTracer => Ok(TransactionTrace::CallListNested(Call::CallTracer(
                CallTracerCall {
                    from: address,
                    trace_address: None,
                    gas: U256::MAX,
                    gas_used: U256::zero(),
                    inner: CallTracerInner::Create {
                        call_type: vec![],
                        input: vec![],
                        to: None,
                        output: None,
                        error: None,
                        value: U256::zero(),
                    },
                    calls: vec![],
                },
            ))),
            _ => Err(format_err!("failed to resolve tracer format").into()),
        },
    }
}
