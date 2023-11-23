use axum::{extract::Query, routing::get, Router};
use serde::Deserialize;

#[derive(Deserialize)]
struct EstimateQuery {
    confirmation_target: i32,
}

async fn estimate_fee(
    Query(EstimateQuery {
        confirmation_target,
    }): Query<EstimateQuery>,
) -> String {
    format!(
        "Fee estimate for confirmation target {}",
        confirmation_target
    )
}

pub fn router() -> Router {
    Router::new().route("/estimate", get(estimate_fee))
}
