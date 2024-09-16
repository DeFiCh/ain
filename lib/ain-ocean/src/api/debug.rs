use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use std::sync::Arc;

use super::AppContext;
use crate::{error::ApiError, Result};

#[ocean_endpoint]
async fn dump_tables(Extension(ctx): Extension<Arc<AppContext>>) -> Result<()> {
    ctx.services.store.dump_table_sizes()
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/dumptables", get(dump_tables))
        .layer(Extension(ctx))
}
