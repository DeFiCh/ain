use axum::{
    extract::{Path, Query},
    routing::get,
    Json, Router,
};
use log::debug;
use serde::{Deserialize, Serialize};

use crate::{
    api_paged_response::ApiPagedResponse,
    api_query::PaginationQuery,
    model::{BlockContext, PoolSwap},
    repository::RepositoryOps,
    Result, SERVICES,
};

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

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapFromToData {
    address: String,
    amount: String,
    // symbol: String,
    // display_symbol: String,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapData {
    id: String,
    sort: String,
    txid: String,
    txno: usize,
    pool_pair_id: String,
    from_amount: String,
    from_token_id: u64,
    block: BlockContext,
    from: PoolSwapFromToData,
    to: PoolSwapFromToData,
}

impl From<PoolSwap> for PoolSwapData {
    fn from(v: PoolSwap) -> Self {
        Self {
            id: v.id,
            sort: v.sort,
            txid: v.txid.to_string(),
            txno: v.txno,
            pool_pair_id: v.pool_id.to_string(),
            from_amount: v.from_amount.to_string(),
            from_token_id: v.from_token_id,
            from: PoolSwapFromToData {
                address: v.from.to_hex_string(),
                amount: v.from_amount.to_string(),
                // symbol: todo!(),
                // display_symbol: todo!(),
            },
            to: PoolSwapFromToData {
                address: v.to.to_hex_string(),
                amount: v.to_amount.to_string(),
                // symbol: todo!(),
                // display_symbol: todo!(),
            },
            block: v.block,
        }
    }
}

async fn list_poolpairs() -> String {
    "List of poolpairs".to_string()
}

async fn get_poolpair(Path(PoolPair { id }): Path<PoolPair>) -> String {
    format!("Details of poolpair with id {}", id)
}

// Use single method for now since additional verbose keys are indexed
// TODO: assess need for additional verbose method
async fn list_pool_swaps(
    Path(id): Path<u32>,
    Query(query): Query<PaginationQuery>,
) -> Result<Json<ApiPagedResponse<PoolSwapData>>> {
    debug!("list_pool_swaps for id {id}",);
    let next = query
        .next
        .map(|q| {
            let parts: Vec<&str> = q.split('-').collect();
            if parts.len() != 2 {
                return Err("Invalid query format");
            }

            let height = parts[0].parse::<u32>().map_err(|_| "Invalid height")?;
            let txno = parts[1].parse::<usize>().map_err(|_| "Invalid txno")?;

            Ok((height, txno))
        })
        .transpose()?
        .unwrap_or_default();

    debug!("next : {:?}", next);

    let size = if query.size > 0 && query.size < 20 {
        query.size
    } else {
        20
    };

    let swaps = SERVICES
        .pool
        .by_id
        .list(Some((id, next.0, next.1)))?
        .take(size)
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == id,
            _ => true,
        })
        .map(|item| {
            let (_, swap) = item?;
            Ok(PoolSwapData::from(swap))
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(Json(ApiPagedResponse::of(swaps, query.size, |swap| {
        swap.sort.to_string()
    })))
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
        .route("/:id/swaps/verbose", get(list_pool_swaps))
        .route(
            "/:id/swaps/aggregate/:interval",
            get(list_pool_swap_aggregates),
        )
        .route("/paths/swappable/:tokenId", get(get_swappable_tokens))
        .route(
            "/paths/best/from/:fromTokenId/to/:toTokenId",
            get(get_best_path),
        )
        .route("/paths/from/:fromTokenId/to/:toTokenId", get(get_all_paths))
        .route("/dexprices", get(list_dex_prices))
}
