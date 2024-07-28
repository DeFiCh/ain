use std::{str::FromStr, sync::Arc};

use axum::{extract::Request, http::StatusCode, middleware::{from_fn, Next}, response::{Response, IntoResponse}, Json, Router};

mod address;
mod block;
mod cache;
pub mod common;
mod fee;
mod governance;
mod loan;
mod masternode;
mod oracle;
mod path;
mod pool_pair;
pub mod prices;
mod query;
mod rawtx;
mod response;
mod stats;
mod tokens;
mod transactions;

use defichain_rpc::Client;
use serde::{Deserialize, Serialize};

use crate::{network::Network, Result, Services};

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
    network: Network,
}

// manual create cors since CorsLayer + axum can only be supported in `tower-http 0.5`
async fn cors(request: Request, next: Next) -> Response {
    let mut response = next.run(request).await;

    let headers = response.headers_mut();
    headers.insert("Access-Control-Allow-Origin", "*".parse().unwrap());
    headers.insert("Access-Control-Allow-Methods", "GET".parse().unwrap());
    headers.insert("Access-Control-Allow-Methods", "POST".parse().unwrap());
    headers.insert("Access-Control-Allow-Headers", "Content-Type".parse().unwrap());
    headers.insert("Access-Control-Max-Age", "10080".parse().unwrap()); // 60 * 24 * 7

    response
}

pub async fn ocean_router(
    services: &Arc<Services>,
    client: Arc<Client>,
    network: String,
) -> Result<Router> {
    let context = Arc::new(AppContext {
        client,
        services: services.clone(),
        network: Network::from_str(&network)?,
    });

    let context_cloned = context.clone();
    tokio::spawn(async move { pool_pair::path::sync_token_graph(&context_cloned).await });

    Ok(Router::new().layer(from_fn(cors)).nest(
        format!("/v0/{}", context.network).as_str(),
        Router::new()
            .nest("/address/", address::router(Arc::clone(&context)))
            .nest("/governance", governance::router(Arc::clone(&context)))
            .nest("/loans", loan::router(Arc::clone(&context)))
            .nest("/fee", fee::router(Arc::clone(&context)))
            .nest("/masternodes", masternode::router(Arc::clone(&context)))
            .nest("/oracles", oracle::router(Arc::clone(&context)))
            .nest("/poolpairs", pool_pair::router(Arc::clone(&context)))
            .nest("/prices", prices::router(Arc::clone(&context)))
            .nest("/rawtx", rawtx::router(Arc::clone(&context)))
            .nest("/stats", stats::router(Arc::clone(&context)))
            .nest("/tokens", tokens::router(Arc::clone(&context)))
            .nest("/transactions", transactions::router(Arc::clone(&context)))
            .nest("/blocks", block::router(Arc::clone(&context)))
            .fallback(not_found),
    ))
}
