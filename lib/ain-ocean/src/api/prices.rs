use axum::{extract::Path, routing::get, Router};
use serde::Deserialize;

#[derive(Deserialize)]
struct PriceKey {
    key: String,
}

#[derive(Deserialize)]
struct FeedWithInterval {
    key: String,
    interval: i64,
}

async fn list_prices() -> String {
    "List of prices".to_string()
}

async fn get_price(Path(PriceKey { key }): Path<PriceKey>) -> String {
    format!("Details of price with key {}", key)
}

async fn get_feed_active(Path(PriceKey { key }): Path<PriceKey>) -> String {
    format!("Active feed for price with key {}", key)
}

async fn get_feed(Path(PriceKey { key }): Path<PriceKey>) -> String {
    format!("Feed for price with key {}", key)
}

async fn get_feed_with_interval(
    Path(FeedWithInterval { key, interval }): Path<FeedWithInterval>,
) -> String {
    format!("Feed for price with key {} over interval {}", key, interval)
}

async fn get_oracles(Path(PriceKey { key }): Path<PriceKey>) -> String {
    format!("Oracles for price with key {}", key)
}

pub fn router() -> Router {
    Router::new()
        .route("/", get(list_prices))
        .route("/:key", get(get_price))
        .route("/:key/feed/active", get(get_feed_active))
        .route("/:key/feed", get(get_feed))
        .route("/:key/feed/interval/:interval", get(get_feed_with_interval))
        .route("/:key/oracles", get(get_oracles))
}
