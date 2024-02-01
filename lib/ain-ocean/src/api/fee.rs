use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use defichain_rpc::{json::mining::SmartFeeEstimation, Client, RpcApi};
use serde::Deserialize;

use super::response::Response;
use crate::{api_query::Query, error::ApiError, Result, Services};

#[derive(Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct EstimateQuery {
    confirmation_target: i32,
}

#[ocean_endpoint]
async fn estimate_fee(
    Query(EstimateQuery {
        confirmation_target,
    }): Query<EstimateQuery>,
    Extension(services): Extension<Arc<Services>>,
) -> Result<Response<f64>> {
    let estimation: SmartFeeEstimation = services.client.call(
        "estimatesmartfee",
        &[confirmation_target.into(), "CONSERVATIVE".into()],
    )?;

    Ok(Response::new(estimation.feerate.unwrap_or(0.00005000)))
}

pub fn router(services: Arc<Services>) -> Router {
    Router::new()
        .route("/estimate", get(estimate_fee))
        .layer(Extension(services))
}
