use std::{net::SocketAddr, str::FromStr, sync::Arc};

use axum::{
    body::Body,
    extract::{ConnectInfo, Request},
    http::{HeaderValue, StatusCode},
    middleware::{from_fn, Next},
    response::{IntoResponse, Response},
    Json, Router,
};

mod address;
mod block;
mod cache;
pub mod common;
mod debug;
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

    let message = format!("Cannot {method} {path}");
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

// NOTE(canonbrother): manually scratch cors since CorsLayer + Axum can only be supported in `tower-http 0.5`
async fn cors(request: Request, next: Next) -> Response {
    let mut response = next.run(request).await;

    response
        .headers_mut()
        .append("Access-Control-Allow-Origin", HeaderValue::from_static("*"));
    response.headers_mut().append(
        "Access-Control-Allow-Methods",
        HeaderValue::from_static("GET"),
    );
    response.headers_mut().append(
        "Access-Control-Allow-Methods",
        HeaderValue::from_static("POST"),
    );
    response.headers_mut().append(
        "Access-Control-Allow-Methods",
        HeaderValue::from_static("PUT"),
    );
    response.headers_mut().append(
        "Access-Control-Allow-Methods",
        HeaderValue::from_static("DELETE"),
    );
    response.headers_mut().append(
        "Access-Control-Allow-Headers",
        HeaderValue::from_static("Content-Type"),
    );
    response
        .headers_mut()
        .append("Access-Control-Max-Age", HeaderValue::from_static("10080")); // 60 * 24 * 7

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
    let main_router = Router::new()
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
        .fallback(not_found);

    let debug_router = Router::new()
        .nest("/debug", debug::router(Arc::clone(&context)))
        .layer(from_fn(localhost_only));

    Ok(Router::new()
        .nest(
            format!("/v0/{}", context.network).as_str(),
            main_router.merge(debug_router),
        )
        .layer(from_fn(cors)))
}

async fn localhost_only(
    req: Request<Body>,
    next: Next,
) -> std::result::Result<Response, StatusCode> {
    let is_localhost = req
        .extensions()
        .get::<ConnectInfo<SocketAddr>>()
        .map(|connect_info| connect_info.ip().is_loopback())
        .unwrap_or_else(|| {
            req.headers()
                .get("X-Forwarded-For")
                .and_then(|addr| addr.to_str().ok())
                .map(|addr| addr.split(',').next().unwrap_or("").trim() == "127.0.0.1")
                .or_else(|| {
                    req.headers()
                        .get("Host")
                        .and_then(|host| host.to_str().ok())
                        .map(|host| {
                            host.starts_with("localhost:") || host.starts_with("127.0.0.1:")
                        })
                })
                .unwrap_or(false)
        });

    if is_localhost {
        Ok(next.run(req).await)
    } else {
        println!("Access denied: Request is not from localhost");
        Err(StatusCode::FORBIDDEN)
    }
}
