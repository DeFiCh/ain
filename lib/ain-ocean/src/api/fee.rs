use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{extract::Query, routing::get, Extension, Json, Router};
use defichain_rpc::{json::mining::SmartFeeEstimation, Client, RpcApi};
use serde::Deserialize;

use super::response::Response;
use crate::{error::ApiError, Result};

#[derive(Deserialize)]
struct EstimateQuery {
    confirmation_target: i32,
}

#[ocean_endpoint]
async fn estimate_fee(
    Query(EstimateQuery {
        confirmation_target,
    }): Query<EstimateQuery>,
    Extension(client): Extension<Arc<Client>>,
) -> Result<Response<f64>> {
    let estimation: SmartFeeEstimation = client.call(
        "estimatesmartfee",
        &[confirmation_target.into(), "CONSERVATIVE".into()],
    )?;
    println!("estimation : {:?}", estimation);

    Ok(Response::new(estimation.feerate.unwrap_or(0.00005000)))
}

pub fn router(state: Arc<Client>) -> Router {
    Router::new()
        .route("/estimate", get(estimate_fee))
        .layer(Extension(state))
}
