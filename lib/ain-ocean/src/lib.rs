pub mod api_paged_response;
pub mod api_query;
pub mod error;
mod indexer;

pub use api::ocean_router;
pub use indexer::{index_block, invalidate_block};
pub mod api;
mod data_acces;
mod model;
pub mod storage;

pub(crate) type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
