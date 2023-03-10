// use std::sync::RwLock;

// use alloc::collections::BTreeMap;
// use evm_runtime::Context;
// use primitive_types::H160;

// use crate::{
// 	backend::MemoryBackend,
// 	executor::stack::{MemoryStackState, StackExecutor},
// };

// const RUNTIME_HANDLE: RwLock<
// 	Option<
// 		&StackExecutor<
// 			MemoryStackState<MemoryBackend>,
// 			BTreeMap<H160, fn(&[u8], Option<u64>, &Context, bool)>,
// 		>,
// 	>,
// > = RwLock::new(None);

// /// Store thread handle so that we can block later for the thread to exit
// pub fn store_handle(
// 	executor: StackExecutor<
// 		MemoryStackState<MemoryBackend>,
// 		BTreeMap<H160, fn(&[u8], Option<u64>, &Context, bool)>,
// 	>,
// ) {
// 	*RUNTIME_HANDLE.write().unwrap() = Some(&executor);
// }
