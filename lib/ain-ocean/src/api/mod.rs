use std::{net::SocketAddr, str::FromStr, sync::Arc, time::Instant};

use axum::{
    body::{to_bytes, Body},
    extract::{ConnectInfo, OriginalUri, Request},
    http::{HeaderMap, HeaderValue, StatusCode},
    middleware::{self, from_fn, Next},
    response::{IntoResponse, Response},
    Json, Router,
};
use log::{debug, log_enabled, trace, Level};

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
mod rpc;
mod stats;
mod tokens;
mod transactions;

use defichain_rpc::Client;
use serde::{Deserialize, Serialize};
use serde_json::json;
use stats::subsidy::{BlockSubsidy, BLOCK_SUBSIDY};

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
    pub block_subsidy: BlockSubsidy,
}

// NOTE(canonbrother): manually scratch cors since CorsLayer + Axum can only be supported in `tower-http 0.5`
async fn cors(request: Request, next: Next) -> Response {
    let mut response = next.run(request).await;

    response
        .headers_mut()
        .append("Access-Control-Allow-Origin", HeaderValue::from_static("*"));
    response.headers_mut().append(
        "Access-Control-Allow-Methods",
        HeaderValue::from_static("GET,POST,PUT,DELETE"),
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
    let network = Network::from_str(&network)?;
    let context = Arc::new(AppContext {
        client,
        services: services.clone(),
        block_subsidy: BLOCK_SUBSIDY.get(&network).cloned().unwrap_or_default(),
        network,
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
        .nest("/rpc", rpc::router(Arc::clone(&context)))
        .fallback(not_found);

    let debug_router = Router::new()
        .nest("/debug", debug::router(Arc::clone(&context)))
        .layer(from_fn(localhost_only));

    Ok(Router::new()
        .nest(
            format!("/v0/{}", context.network).as_str(),
            main_router.merge(debug_router),
        )
        .layer(from_fn(cors))
        .layer(middleware::from_fn(logging_middleware)))
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

const MAX_BODY_SIZE: usize = 1024 * 1024 * 16; // 16MB limit for body logging

async fn logging_middleware(
    OriginalUri(original_uri): OriginalUri,
    req: Request<Body>,
    next: Next,
) -> std::result::Result<impl IntoResponse, StatusCode> {
    let method = req.method().clone();
    let path = req.uri().path().to_owned();
    let query = req.uri().query().unwrap_or("").to_owned();

    debug!("Request: {} {}", method, path);

    if log_enabled!(Level::Trace) {
        let headers = format_headers(req.headers());
        let request_log = json!({
            "method": method.as_str(),
            "path": path,
            "query": query,
            "headers": headers,
            "original_uri": original_uri.to_string(),
        });
        if let Ok(json) = serde_json::to_string(&request_log) {
            trace!("Request: {json}");
        }
    }

    let start = Instant::now();
    let res = next.run(req).await;
    let latency = start.elapsed();

    debug!("Response: {} {} {:?}", method, path, latency);

    if log_enabled!(Level::Trace) {
        let (parts, body) = res.into_parts();
        let bytes = to_bytes(body, MAX_BODY_SIZE).await.unwrap_or_default();
        let body_str = String::from_utf8_lossy(&bytes);

        let response_log = json!({
            "status": parts.status.as_u16(),
            "headers": format_headers(&parts.headers),
            "body": body_str,
        });
        if let Ok(json) = serde_json::to_string(&response_log) {
            trace!("Response: {json}",);
        }

        Ok(Response::from_parts(parts, Body::from(bytes)))
    } else {
        Ok(res)
    }
}

fn format_headers(headers: &HeaderMap) -> serde_json::Value {
    let mut map = serde_json::Map::new();
    for (key, value) in headers.iter() {
        if let Ok(v) = value.to_str() {
            map.insert(
                key.as_str().to_owned(),
                serde_json::Value::String(v.to_owned()),
            );
        }
    }
    serde_json::Value::Object(map)
}
