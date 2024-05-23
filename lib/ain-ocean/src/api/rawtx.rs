use std::sync::Arc;
use crate::{
    error::{ApiError, Error, NotFoundKind},
    model::RawTx,
    repository::RepositoryOps,
    storage::SortOrder,
    Result,
};
use super::{
    query::PaginationQuery,
    response::{ApiPagedResponse, Response},
    AppContext,
};
use axum::{
    extract::{Path, Query},
    routing::{get,post},
    Extension, Router,
};
use defichain_rpc::{Client, RpcApi};

use ain_macros::ocean_endpoint;

#[ocean_endpoint]
async fn send_rawtx(
    Path(tx): Path<RawTx>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<String>> {
    
    Ok(Response::new("Sending raw transaction".to_string()))
}

#[ocean_endpoint]
async fn test_rawtx(
    Path(tx): Path<RawTx>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<String>> {
 
Ok(Response::new("Testing raw transaction".to_string()))
}
#[ocean_endpoint]
async fn get_rawtx(
    Path(txid): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<String>> {
   format!("Details of raw transaction with txid {}", txid);
   Ok(Response::new("Testing raw transaction".to_string()))
}

async fn validate(hex:String) {
   if !hex.starts_with("040000000001") {
      return
    }
   
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/send", post(send_rawtx))
        .route("/test", get(test_rawtx))
        .route("/:txid", get(get_rawtx))
        .layer(Extension(ctx))
}
