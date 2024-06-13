use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use defichain_rpc::{json::mining::SmartFeeEstimation, RpcApi};
use serde::Deserialize;

use super::{query::Query, response::Response, AppContext};
use crate::{error::ApiError, Result};

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
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<f64>> {
    let estimation: SmartFeeEstimation = ctx
        .client
        .call(
            "estimatesmartfee",
            &[confirmation_target.into(), "CONSERVATIVE".into()],
        )
        .await?;

    Ok(Response::new(estimation.feerate.unwrap_or(0.00005000)))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/estimate", get(estimate_fee))
        .layer(Extension(ctx))
}
