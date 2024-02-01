use std::sync::Arc;

use axum::{extract::Request, http::StatusCode, response::IntoResponse, Json, Router};

// mod address;
mod block;
mod fee;
mod governance;
// mod loan;
// mod masternode;
// mod oracle;
// mod poolpairs;
// mod prices;
// mod rawtx;
// mod stats;
mod common;
mod response;
mod tokens;
// mod transactions;
use axum::routing::get;
use serde::{Deserialize, Serialize};

use crate::{Result, SERVICES};

async fn ocean_not_activated() -> impl IntoResponse {
    (StatusCode::FORBIDDEN, "Ocean is not activated")
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
struct NotFound {
    status_code: u16,
    message: String,
    error: &'static str,
}

async fn not_found(req: Request<axum::body::Body>) -> impl IntoResponse {
    let method = req.method().clone();
    let path = req.uri().path().to_string();

    let message = format!("Cannot {} {}", method, path);
    (
        StatusCode::NOT_FOUND,
        Json(NotFound {
            status_code: StatusCode::NOT_FOUND.as_u16(),
            message,
            error: "Not found",
        }),
    )
}

pub fn ocean_router() -> Result<Router> {
    if !ain_cpp_imports::is_ocean_rest_enabled() {
        return Ok(Router::new().route("/*path", get(ocean_not_activated)));
    }

    let services = &SERVICES;

    Ok(Router::new()
        // .nest("/address", address::router(Arc::clone(services)))
        .nest("/governance", governance::router(Arc::clone(services)))
        // .nest("/loans", loan::router(Arc::clone(services)))
        .nest("/fee", fee::router(Arc::clone(services)))
        // .nest("/masternodes", masternode::router(Arc::clone(services)))
        // .nest("/oracles", oracle::router(Arc::clone(services)))
        // .nest("/poolpairs", poolpairs::router(Arc::clone(services)))
        // .nest("/prices", prices::router(Arc::clone(services)))
        // .nest("/rawtx", rawtx::router(Arc::clone(services)))
        // .nest("/stats", stats::router(Arc::clone(services)))
        .nest("/tokens", tokens::router(Arc::clone(services)))
        // .nest("/transactions", transactions::router(Arc::clone(services)))
        .nest("/blocks", block::router(Arc::clone(services)))
        .fallback(not_found))
}
