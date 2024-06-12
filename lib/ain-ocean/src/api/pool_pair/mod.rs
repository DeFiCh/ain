use std::{
    collections::{HashMap, HashSet},
    sync::Arc,
};

use ain_macros::ocean_endpoint;
use anyhow::format_err;
use axum::{routing::get, Extension, Router};
use defichain_rpc::{
    json::{
        poolpair::{PoolPairInfo, PoolPairsResult},
        token::TokenInfo,
    },
    RpcApi,
};
use futures::future::try_join_all;
use path::{
    get_all_swap_paths, get_token_identifier, sync_token_graph_if_empty, BestSwapPathResponse,
    SwapPathsResponse,
};
use petgraph::graphmap::UnGraphMap;
use rust_decimal::Decimal;
use serde::{Deserialize, Serialize};
use serde_json::json;
use serde_with::skip_serializing_none;
use service::{
    check_swap_type, find_swap_from_to, get_aggregated_in_usd, get_apr, get_total_liquidity_usd,
    get_usd_volume, PoolPairVolumeResponse, PoolSwapFromTo, PoolSwapFromToData, SwapType,
};

use super::{
    cache::{get_pool_pair_cached, get_token_cached},
    common::parse_dat_symbol,
    path::Path,
    query::{PaginationQuery, Query},
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    error::{ApiError, Error, NotFoundKind},
    model::{BlockContext, PoolSwap, PoolSwapAggregated},
    repository::{InitialKeyProvider, PoolSwapRepository, RepositoryOps, SecondaryIndex},
    storage::SortOrder,
    Result, TokenIdentifier,
};

use price::DexPriceResponse;

pub mod path;
pub mod price;
pub mod service;

// #[derive(Deserialize)]
// struct PoolPair {
//     id: String,
// }

#[derive(Deserialize)]
struct SwapAggregate {
    id: String,
    interval: u32,
}

#[derive(Debug, Deserialize, Default)]
struct DexPrices {
    denomination: String,
}

#[skip_serializing_none]
#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapVerboseResponse {
    id: String,
    sort: String,
    txid: String,
    txno: usize,
    pool_pair_id: String,
    from_amount: String,
    from_token_id: u64,
    block: BlockContext,
    from: Option<PoolSwapFromToData>,
    to: Option<PoolSwapFromToData>,
    r#type: Option<SwapType>,
}

impl PoolSwapVerboseResponse {
    fn map(v: PoolSwap, from_to: Option<PoolSwapFromTo>, swap_type: Option<SwapType>) -> Self {
        Self {
            id: v.id,
            sort: v.sort,
            txid: v.txid.to_string(),
            txno: v.txno,
            pool_pair_id: v.pool_id.to_string(),
            from_amount: Decimal::new(v.from_amount, 8).to_string(),
            from_token_id: v.from_token_id,
            from: from_to.clone().and_then(|item| item.from),
            to: from_to.and_then(|item| item.to),
            block: v.block,
            r#type: swap_type,
        }
    }
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapResponse {
    id: String,
    sort: String,
    txid: String,
    txno: usize,
    pool_pair_id: String,
    from_amount: String,
    from_token_id: u64,
    block: BlockContext,
}

impl From<PoolSwap> for PoolSwapResponse {
    fn from(v: PoolSwap) -> Self {
        Self {
            id: v.id,
            sort: v.sort,
            txid: v.txid.to_string(),
            txno: v.txno,
            pool_pair_id: v.pool_id.to_string(),
            from_amount: Decimal::new(v.from_amount, 8).to_string(),
            from_token_id: v.from_token_id,
            block: v.block,
        }
    }
}

#[derive(Serialize, Debug, Clone, Default)]
#[serde(rename_all = "camelCase")]
struct PoolPairFeeResponse {
    pct: Option<String>,
    in_pct: Option<String>,
    out_pct: Option<String>,
}

#[derive(Serialize, Debug, Clone, Default)]
#[serde(rename_all = "camelCase")]
struct PoolPairTokenResponse {
    id: String,
    name: String,
    symbol: String,
    display_symbol: String,
    reserve: String,
    block_commission: String,
    fee: Option<PoolPairFeeResponse>,
}

#[derive(Serialize, Debug, Clone, Default)]
struct PoolPairPriceRatioResponse {
    ab: String,
    ba: String,
}

#[derive(Serialize, Debug, Clone, Default)]
struct PoolPairTotalLiquidityResponse {
    token: Option<String>,
    usd: Option<String>,
}

#[derive(Serialize, Debug, Clone, Default)]
struct PoolPairCreationResponse {
    tx: String,
    height: i64,
}

#[derive(Serialize, Debug, Clone, Default)]
pub struct PoolPairAprResponse {
    pub total: Decimal,
    pub reward: Decimal,
    pub commission: Decimal,
}

#[derive(Serialize, Debug, Clone, Default)]
#[serde(rename_all = "camelCase")]
pub struct PoolPairResponse {
    id: String,
    symbol: String,
    display_symbol: String,
    name: String,
    status: bool,
    token_a: PoolPairTokenResponse,
    token_b: PoolPairTokenResponse,
    price_ratio: PoolPairPriceRatioResponse,
    commission: String,
    total_liquidity: PoolPairTotalLiquidityResponse,
    trade_enabled: bool,
    owner_address: String,
    reward_pct: String,
    reward_loan_pct: String,
    custom_rewards: Option<Vec<String>>,
    creation: PoolPairCreationResponse,
    apr: PoolPairAprResponse,
    volume: PoolPairVolumeResponse,
}

impl PoolPairResponse {
    pub fn from_with_id(
        id: String,
        p: PoolPairInfo,
        a_token_name: String,
        b_token_name: String,
        total_liquidity_usd: Decimal,
        apr: PoolPairAprResponse,
        volume: PoolPairVolumeResponse,
    ) -> Result<Self> {
        let parts = p.symbol.split('-').collect::<Vec<&str>>();
        let [a, b] = <[&str; 2]>::try_from(parts)
            .map_err(|_| format_err!("Invalid pool pair symbol structure"))?;
        let a_parsed = parse_dat_symbol(a);
        let b_parsed = parse_dat_symbol(b);

        Ok(Self {
            id,
            symbol: p.symbol.clone(),
            display_symbol: format!("{a_parsed}-{b_parsed}"),
            name: p.name,
            status: p.status,
            token_a: PoolPairTokenResponse {
                symbol: a.to_string(),
                display_symbol: a_parsed,
                id: p.id_token_a,
                name: a_token_name,
                reserve: p.reserve_a.to_string(),
                block_commission: p.block_commission_a.to_string(),
                fee: p.dex_fee_pct_token_a.map(|_| PoolPairFeeResponse {
                    pct: p.dex_fee_pct_token_a.map(|fee| fee.to_string()),
                    in_pct: p.dex_fee_in_pct_token_a.map(|fee| fee.to_string()),
                    out_pct: p.dex_fee_out_pct_token_a.map(|fee| fee.to_string()),
                }),
            },
            token_b: PoolPairTokenResponse {
                symbol: b.to_string(),
                display_symbol: b_parsed,
                id: p.id_token_b,
                name: b_token_name,
                reserve: p.reserve_b.to_string(),
                block_commission: p.block_commission_b.to_string(),
                fee: p.dex_fee_pct_token_b.map(|_| PoolPairFeeResponse {
                    pct: p.dex_fee_pct_token_b.map(|fee| fee.to_string()),
                    in_pct: p.dex_fee_in_pct_token_b.map(|fee| fee.to_string()),
                    out_pct: p.dex_fee_out_pct_token_b.map(|fee| fee.to_string()),
                }),
            },
            price_ratio: PoolPairPriceRatioResponse {
                ab: p.reserve_a_reserve_b.to_string(),
                ba: p.reserve_b_reserve_a.to_string(),
            },
            commission: p.commission.to_string(),
            total_liquidity: PoolPairTotalLiquidityResponse {
                token: Some(p.total_liquidity.to_string()),
                usd: Some(total_liquidity_usd.to_string()),
            },
            trade_enabled: p.trade_enabled,
            owner_address: p.owner_address,
            reward_pct: p.reward_pct.to_string(),
            reward_loan_pct: p.reward_loan_pct.to_string(),
            custom_rewards: p.custom_rewards,
            creation: PoolPairCreationResponse {
                tx: p.creation_tx,
                height: p.creation_height,
            },
            apr,
            volume,
        })
    }
}

#[ocean_endpoint]
async fn list_pool_pairs(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<PoolPairResponse>> {
    let pools: PoolPairsResult = ctx.client.call(
        "listpoolpairs",
        &[
            json!({
                "limit": query.size,
                "start": query.next.as_ref().and_then(|n| n.parse::<u32>().ok()).unwrap_or_default(),
                "including_start": query.next.is_none()
            }),
            true.into(),
        ],
    ).await?;

    let fut = pools
        .0
        .into_iter()
        .filter(|(_, p)| !p.symbol.starts_with("BURN-"))
        .map(|(id, p)| async {
            let (
                _,
                TokenInfo {
                    name: a_token_name, ..
                },
            ) = get_token_cached(&ctx, &p.id_token_a)
                .await?
                .ok_or(format_err!("None is not valid"))?;
            let (
                _,
                TokenInfo {
                    name: b_token_name, ..
                },
            ) = get_token_cached(&ctx, &p.id_token_b)
                .await?
                .ok_or(format_err!("None is not valid"))?;

            let total_liquidity_usd = get_total_liquidity_usd(&ctx, &p).await?;
            let apr = get_apr(&ctx, &id, &p).await?;
            let volume = get_usd_volume(&ctx, &id).await?;
            let res = PoolPairResponse::from_with_id(
                id,
                p,
                a_token_name,
                b_token_name,
                total_liquidity_usd,
                apr,
                volume,
            )?;
            Ok::<PoolPairResponse, Error>(res)
        })
        .collect::<Vec<_>>();

    let res = try_join_all(fut).await?;

    Ok(ApiPagedResponse::of(res, query.size, |pool| {
        pool.id.clone()
    }))
}

#[ocean_endpoint]
async fn get_pool_pair(
    Path(id): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<Option<PoolPairResponse>>> {
    if let Some((id, pool)) = get_pool_pair_cached(&ctx, id).await? {
        let total_liquidity_usd = get_total_liquidity_usd(&ctx, &pool).await?;
        let apr = get_apr(&ctx, &id, &pool).await?;
        let volume = get_usd_volume(&ctx, &id).await?;
        let (
            _,
            TokenInfo {
                name: a_token_name, ..
            },
        ) = get_token_cached(&ctx, &pool.id_token_a)
            .await?
            .ok_or(format_err!("None is not valid"))?;
        let (
            _,
            TokenInfo {
                name: b_token_name, ..
            },
        ) = get_token_cached(&ctx, &pool.id_token_b)
            .await?
            .ok_or(format_err!("None is not valid"))?;
        let res = PoolPairResponse::from_with_id(
            id,
            pool,
            a_token_name,
            b_token_name,
            total_liquidity_usd,
            apr,
            volume,
        )?;
        return Ok(Response::new(Some(res)));
    };

    Err(Error::NotFound(NotFoundKind::PoolPair))
}

#[ocean_endpoint]
async fn list_pool_swaps(
    Path(id): Path<u32>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<PoolSwapResponse>> {
    let next = query
        .next
        .as_ref()
        .map(|q| {
            let parts: Vec<&str> = q.split('-').collect();
            if parts.len() != 2 {
                return Err("Invalid query format");
            }

            let height = parts[0].parse::<u32>().map_err(|_| "Invalid height")?;
            let txno = parts[1].parse::<usize>().map_err(|_| "Invalid txno")?;

            Ok((id, height, txno))
        })
        .transpose()?
        .unwrap_or(PoolSwapRepository::initial_key(id));

    let size = if query.size > 200 { 200 } else { query.size };

    let swaps = ctx
        .services
        .pool
        .by_id
        .list(Some(next), SortOrder::Descending)?
        .take(size)
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == id,
            _ => true,
        })
        .map(|item| {
            let (_, swap) = item?;
            Ok(PoolSwapResponse::from(swap))
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(swaps, query.size, |swap| {
        swap.sort.to_string()
    }))
}

#[ocean_endpoint]
async fn list_pool_swaps_verbose(
    Path(id): Path<u32>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<PoolSwapVerboseResponse>> {
    let next = query
        .next
        .as_ref()
        .map(|q| {
            let parts: Vec<&str> = q.split('-').collect();
            if parts.len() != 2 {
                return Err("Invalid query format");
            }

            let height = parts[0].parse::<u32>().map_err(|_| "Invalid height")?;
            let txno = parts[1].parse::<usize>().map_err(|_| "Invalid txno")?;

            Ok((id, height, txno))
        })
        .transpose()?
        .unwrap_or(PoolSwapRepository::initial_key(id));

    let size = if query.size > 20 { 20 } else { query.size };

    let fut = ctx
        .services
        .pool
        .by_id
        .list(Some(next), SortOrder::Descending)?
        .take(size)
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == id,
            _ => true,
        })
        .map(|item| async {
            let (_, swap) = item?;
            let from_to =
                find_swap_from_to(&ctx, swap.block.height, swap.txid, swap.txno.try_into()?)
                    .await?;

            let swap_type = check_swap_type(&ctx, swap.clone()).await?;

            let res = PoolSwapVerboseResponse::map(swap, from_to, swap_type);
            Ok::<PoolSwapVerboseResponse, Error>(res)
        })
        .collect::<Vec<_>>();

    let swaps = try_join_all(fut).await?;

    Ok(ApiPagedResponse::of(swaps, query.size, |swap| {
        swap.sort.to_string()
    }))
}

#[derive(Serialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
struct PoolSwapAggregatedAggregatedResponse {
    amounts: HashMap<String, String>,
    usd: Decimal,
}

#[derive(Serialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
struct PoolSwapAggregatedResponse {
    id: String,
    key: String,
    bucket: i64,
    aggregated: PoolSwapAggregatedAggregatedResponse,
    block: BlockContext,
}

impl PoolSwapAggregatedResponse {
    fn with_usd(p: PoolSwapAggregated, usd: Decimal) -> Self {
        Self {
            id: p.id,
            key: p.key,
            bucket: p.bucket,
            aggregated: PoolSwapAggregatedAggregatedResponse {
                amounts: p.aggregated.amounts,
                usd,
            },
            block: p.block,
        }
    }
}

#[ocean_endpoint]
async fn list_pool_swap_aggregates(
    Path(SwapAggregate { id, interval }): Path<SwapAggregate>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<PoolSwapAggregatedResponse>> {
    let pool_id = id.parse::<u32>()?;

    // bucket
    let next = query
        .next
        .map(|bucket| {
            let bucket = bucket.parse::<i64>()?;
            Ok::<i64, Error>(bucket)
        })
        .transpose()?
        .unwrap_or(i64::MAX);

    let repository = &ctx.services.pool_swap_aggregated;
    let aggregates = repository
        .by_key
        .list(Some((pool_id, interval, next)), SortOrder::Descending)?
        .take(query.size)
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == pool_id && k.1 == interval,
            _ => true,
        })
        .map(|e| repository.by_key.retrieve_primary_value(e))
        .collect::<Result<Vec<_>>>()?;

    let mut aggregated_usd = Vec::<PoolSwapAggregatedResponse>::new();
    for aggregated in aggregates {
        let usd = get_aggregated_in_usd(&ctx, &aggregated.aggregated).await?;
        let aggregate_with_usd = PoolSwapAggregatedResponse::with_usd(aggregated, usd);
        aggregated_usd.push(aggregate_with_usd)
    }

    Ok(ApiPagedResponse::of(
        aggregated_usd,
        query.size,
        |aggregated| aggregated.bucket,
    ))
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct AllSwappableTokensResponse {
    from_token: TokenIdentifier,
    swappable_tokens: Vec<TokenIdentifier>,
}

#[ocean_endpoint]
async fn get_swappable_tokens(
    Path(token_id): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<AllSwappableTokensResponse>> {
    sync_token_graph_if_empty(&ctx).await?;

    let mut token_ids: HashSet<u32> = HashSet::new();

    fn recur(graph: &UnGraphMap<u32, String>, token_ids: &mut HashSet<u32>, token_id: u32) {
        if token_ids.contains(&token_id) {
            return;
        };
        token_ids.insert(token_id);
        let edges = graph.edges(token_id).collect::<Vec<_>>();
        for edge in edges {
            recur(graph, token_ids, edge.0);
            recur(graph, token_ids, edge.1);
        }
    }

    {
        let graph = ctx.services.token_graph.lock().clone();
        recur(&graph, &mut token_ids, token_id.parse::<u32>()?);
    }

    token_ids.remove(&token_id.parse::<u32>()?);

    let mut swappable_tokens = Vec::new();
    for id in token_ids.into_iter() {
        let token = get_token_identifier(&ctx, &id.to_string()).await?;
        swappable_tokens.push(token);
    }

    Ok(Response::new(AllSwappableTokensResponse {
        from_token: get_token_identifier(&ctx, &token_id).await?,
        swappable_tokens,
    }))
}

#[ocean_endpoint]
async fn list_paths(
    Path((token_id, to_token_id)): Path<(String, String)>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<SwapPathsResponse>> {
    let res = get_all_swap_paths(&ctx, &token_id, &to_token_id).await?;

    Ok(Response::new(res))
}

#[ocean_endpoint]
async fn get_best_path(
    Path((from_token_id, to_token_id)): Path<(String, String)>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<BestSwapPathResponse>> {
    let res = path::get_best_path(&ctx, &from_token_id, &to_token_id).await?;
    Ok(Response::new(res))
}

#[ocean_endpoint]
async fn list_dex_prices(
    Query(DexPrices { denomination }): Query<DexPrices>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<DexPriceResponse>> {
    let prices = price::list_dex_prices(&ctx, denomination).await?;

    Ok(Response::new(prices))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/", get(list_pool_pairs))
        .route("/:id", get(get_pool_pair))
        .route("/:id/swaps", get(list_pool_swaps))
        .route("/:id/swaps/verbose", get(list_pool_swaps_verbose))
        .route("/paths/from/:fromTokenId/to/:toTokenId", get(list_paths))
        .route(
            "/paths/best/from/:fromTokenId/to/:toTokenId",
            get(get_best_path),
        )
        .route(
            "/:id/swaps/aggregate/:interval",
            get(list_pool_swap_aggregates),
        )
        .route("/paths/swappable/:tokenId", get(get_swappable_tokens))
        .route("/dexprices", get(list_dex_prices))
        .layer(Extension(ctx))
}
