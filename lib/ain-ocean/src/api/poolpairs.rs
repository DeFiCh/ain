use axum::{
    extract::{Path, Query},
    routing::get,
    Router,
};
use serde::Deserialize;

#[derive(Deserialize)]
struct PoolPair {
    id: String,
}

#[derive(Deserialize)]
struct SwapAggregate {
    id: String,
    interval: i64,
}

#[derive(Deserialize)]
struct SwappableTokens {
    token_id: String,
}

#[derive(Deserialize)]
struct BestPath {
    from_token_id: String,
    to_token_id: String,
}

#[derive(Debug, Deserialize)]
struct DexPrices {
    denomination: Option<String>,
}

async fn list_poolpairs() -> String {
    "List of poolpairs".to_string()
}

async fn get_poolpair(Path(PoolPair { id }): Path<PoolPair>) -> String {
    format!("Details of poolpair with id {}", id)
}

async fn list_pool_swaps(Path(PoolPair { id }): Path<PoolPair>) -> String {
    format!("List of swaps for poolpair {}", id)
}

async fn list_pool_swaps_verbose(Path(PoolPair { id }): Path<PoolPair>) -> String {
    format!("Verbose list of swaps for poolpair {}", id)
}

async fn list_pool_swap_aggregates(
    Path(SwapAggregate { id, interval }): Path<SwapAggregate>,
) -> String {
    format!(
        "Aggregate swaps for poolpair {} over interval {}",
        id, interval
    )
}

async fn get_swappable_tokens(Path(SwappableTokens { token_id }): Path<SwappableTokens>) -> String {
    format!("Swappable tokens for token id {}", token_id)
}

async fn get_best_path(
    Query(BestPath {
        from_token_id,
        to_token_id,
    }): Query<BestPath>,
) -> String {
    format!(
        "Best path from token id {} to {}",
        from_token_id, to_token_id
    )
}

async fn get_all_paths(
    Query(BestPath {
        from_token_id,
        to_token_id,
    }): Query<BestPath>,
) -> String {
    format!(
        "All paths from token id {} to {}",
        from_token_id, to_token_id
    )
}

async fn list_dex_prices(Query(DexPrices { denomination }): Query<DexPrices>) -> String {
    format!("List of DEX prices with denomination {:?}", denomination)
}

pub fn router() -> Router {
    Router::new()
        .route("/", get(list_poolpairs))
        .route("/:id", get(get_poolpair))
        .route("/:id/swaps", get(list_pool_swaps))
        .route("/:id/swaps/verbose", get(list_pool_swaps_verbose))
        .route(
            "/:id/swaps/aggregate/:interval",
            get(list_pool_swap_aggregates),
        )
        .route("/paths/swappable/:tokenId", get(get_swappable_tokens))
        .route("/paths/best/from/:fromTokenId/to/:toTokenId", get(get_best_path))
        .route("/paths/from/:fromTokenId/to/:toTokenId", get(get_all_paths))
        .route("/dexprices", get(list_dex_prices))
}
