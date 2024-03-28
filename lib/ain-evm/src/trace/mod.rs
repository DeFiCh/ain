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

use tracing::{Event, EvmEvent, GasometerEvent, Listener, RuntimeEvent, StepEventFilter};

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
