use std::sync::Arc;

use axum::{extract::Request, http::StatusCode, response::IntoResponse, Json, Router};

// mod address;
mod block;
mod fee;
mod governance;
mod loan;
mod masternode;
// mod oracle;
mod poolpairs;
// mod prices;
// mod rawtx;
mod cache;
pub mod common;
mod path;
mod query;
mod response;
mod stats;
mod tokens;
mod transactions;

use defichain_rpc::Client;
use serde::{Deserialize, Serialize};

use crate::{Result, Services};

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

pub struct AppContext {
    services: Arc<Services>,
    client: Arc<Client>,
    network: String, // TODO Proper handling of network
}

pub async fn ocean_router(
    services: &Arc<Services>,
    client: Arc<Client>,
    network: String,
) -> Result<Router> {
    let context = Arc::new(AppContext {
        client,
        services: services.clone(),
        network,
    });

    Ok(Router::new().nest(
        format!("/v0/{}", context.network).as_str(),
        Router::new()
            // .nest("/address/", address::router(Arc::clone(&context)))
            .nest("/governance", governance::router(Arc::clone(&context)))
            .nest("/loans", loan::router(Arc::clone(&context)))
            .nest("/fee", fee::router(Arc::clone(&context)))
            .nest("/masternodes", masternode::router(Arc::clone(&context)))
            // .nest("/oracles", oracle::router(Arc::clone(&context)))
            .nest("/poolpairs", poolpairs::router(Arc::clone(&context)))
            // .nest("/prices", prices::router(Arc::clone(&context)))
            // .nest("/rawtx", rawtx::router(Arc::clone(&context)))
            .nest("/stats", stats::router(Arc::clone(&context)))
            .nest("/tokens", tokens::router(Arc::clone(&context)))
            .nest("/transactions", transactions::router(Arc::clone(&context)))
            .nest("/blocks", block::router(Arc::clone(&context)))
            .fallback(not_found),
    ))
}
