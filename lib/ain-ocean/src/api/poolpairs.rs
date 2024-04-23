use std::{collections::HashSet, sync::Arc};

use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use defichain_rpc::{
    json::poolpair::{PoolPairInfo, PoolPairsResult},
    RpcApi,
};
use serde::{Deserialize, Serialize};
use serde_json::json;
use rust_decimal_macros::dec;
use anyhow::format_err;

use super::{
    common::{format_number, parse_dat_symbol},
    path::Path,
    poolpairs_path::{compute_paths_between_tokens, compute_return_less_dex_fees_in_destination_token, get_token_identifier, EstimatedLessDexFeeInfo, SwapPathPoolPair},
    query::{PaginationQuery, Query},
    response::{ApiPagedResponse, Response},
    AppContext,
};

use crate::{
    error::ApiError,
    model::{BlockContext, PoolSwap},
    repository::{InitialKeyProvider, PoolSwapRepository, RepositoryOps},
    storage::SortOrder,
    Error, Result, TokenIdentifier,
};

// #[derive(Deserialize)]
// struct PoolPair {
//     id: String,
// }

// #[derive(Deserialize)]
// struct SwapAggregate {
//     id: String,
//     interval: i64,
// }

// #[derive(Debug, Deserialize)]
// struct DexPrices {
//     denomination: Option<String>,
// }

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapFromToResponse {
    address: String,
    amount: String,
    // symbol: String,
    // display_symbol: String,
}

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
    from: PoolSwapFromToResponse,
    to: PoolSwapFromToResponse,
    // type: todo()!
}

impl From<PoolSwap> for PoolSwapVerboseResponse {
    fn from(v: PoolSwap) -> Self {
        Self {
            id: v.id,
            sort: v.sort,
            txid: v.txid.to_string(),
            txno: v.txno,
            pool_pair_id: v.pool_id.to_string(),
            from_amount: v.from_amount.to_string(),
            from_token_id: v.from_token_id,
            from: PoolSwapFromToResponse {
                address: v.from.to_hex_string(),
                amount: v.from_amount.to_string(),
                // symbol: todo!(),
                // display_symbol: todo!(),
            },
            to: PoolSwapFromToResponse {
                address: v.to.to_hex_string(),
                amount: v.to_amount.to_string(),
                // symbol: todo!(),
                // display_symbol: todo!(),
            },
            block: v.block,
            // type: todo!(),
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
            from_amount: v.from_amount.to_string(),
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
struct PoolPairAprResponse {
    total: f64,
    reward: f64,
    commission: f64,
}

#[derive(Serialize, Debug, Clone, Default)]
struct PoolPairVolumeResponse {
    d30: f64,
    h24: f64,
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
    apr: Option<PoolPairAprResponse>,
    volume: Option<PoolPairVolumeResponse>,
}

impl PoolPairResponse {
    pub fn from_with_id(id: String, p: PoolPairInfo) -> Self {
        let parts = p.symbol.split('-').collect::<Vec<&str>>();
        let [a, b] = <[&str; 2]>::try_from(parts).ok().unwrap();
        let a_parsed = parse_dat_symbol(a);
        let b_parsed = parse_dat_symbol(b);

        Self {
            id,
            symbol: p.symbol.clone(),
            display_symbol: format!("{a_parsed}-{b_parsed}"),
            name: p.name,
            status: p.status,
            token_a: PoolPairTokenResponse {
                symbol: a.to_string(),
                display_symbol: a_parsed,
                id: p.id_token_a,
                name: "".to_string(), // todo: (await this.deFiDCache.getTokenInfo(info.idTokenA) as TokenInfo).name
                reserve: p.reserve_a.to_string(),
                block_commission: p.block_commission_a.to_string(),
                fee: p.dex_fee_in_pct_token_a.map(|_| PoolPairFeeResponse {
                    pct: Some(p.dex_fee_pct_token_a.unwrap().to_string()),
                    in_pct: Some(p.dex_fee_in_pct_token_a.unwrap().to_string()),
                    out_pct: Some(p.dex_fee_out_pct_token_a.unwrap().to_string()),
                }),
            },
            token_b: PoolPairTokenResponse {
                symbol: b.to_string(),
                display_symbol: b_parsed,
                id: p.id_token_b,
                name: "".to_string(), // todo: (await this.deFiDCache.getTokenInfo(info.idTokenB) as TokenInfo).name
                reserve: p.reserve_b.to_string(),
                block_commission: p.block_commission_b.to_string(),
                fee: p.dex_fee_in_pct_token_b.map(|_| PoolPairFeeResponse {
                    pct: Some(p.dex_fee_pct_token_b.unwrap().to_string()),
                    in_pct: Some(p.dex_fee_in_pct_token_b.unwrap().to_string()),
                    out_pct: Some(p.dex_fee_out_pct_token_b.unwrap().to_string()),
                }),
            },
            price_ratio: PoolPairPriceRatioResponse {
                ab: p.reserve_a_reserve_b.to_string(),
                ba: p.reserve_b_reserve_a.to_string(),
            },
            commission: p.commission.to_string(),
            total_liquidity: PoolPairTotalLiquidityResponse {
                token: Some(p.total_liquidity.to_string()),
                usd: None, // todo: await this.poolPairService.getTotalLiquidityUsd(info)
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
            apr: None,    // todo: await this.poolPairService.getAPR(id, info)
            volume: None, // todo: await this.poolPairService.getUSDVolume(id)
        }
    }
}

#[ocean_endpoint]
async fn list_poolpairs(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<PoolPairResponse>> {
    let poolpairs: PoolPairsResult = ctx.client.call(
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

    let res = poolpairs
        .0
        .into_iter()
        .filter_map(|(k, v)| {
            if v.symbol.starts_with("BURN-") {
                None
            } else {
                Some(PoolPairResponse::from_with_id(k, v))
            }
        })
        .collect::<Vec<_>>();

    Ok(ApiPagedResponse::of(res, query.size, |poolpair| {
        poolpair.id.clone()
    }))
}

#[ocean_endpoint]
async fn get_poolpair(
    Path(id): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<Option<PoolPairResponse>>> {
    let mut poolpair: PoolPairsResult = ctx
        .client
        .call("getpoolpair", &[id.as_str().into()])
        .await?;

    let res = poolpair
        .0
        .remove(&id)
        .map(|poolpair| PoolPairResponse::from_with_id(id, poolpair));

    Ok(Response::new(res))
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
            Ok(PoolSwapVerboseResponse::from(swap))
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(swaps, query.size, |swap| {
        swap.sort.to_string()
    }))
}

// #[ocean_endpoint]
// async fn list_pool_swap_aggregates(
//     Path(SwapAggregate { id, interval }): Path<SwapAggregate>,
// ) -> String {
//     format!(
//         "Aggregate swaps for poolpair {} over interval {}",
//         id, interval
//     )
// }

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
    let mut token_ids: HashSet<u32> = HashSet::new();
    {
        let graph = &ctx.services.token_graph.lock();
        let edges = graph.edges(token_id.parse::<u32>()?).collect::<Vec<_>>();
        for edge in edges {
            token_ids.insert(edge.0);
            token_ids.insert(edge.1);
        }
    }

    let mut swappable_tokens = Vec::new();
    for id in token_ids.into_iter() {
        let token = get_token_identifier(&ctx, &id.to_string()).await?;
        swappable_tokens.push(token);
    }

    Ok(Response::new(AllSwappableTokensResponse{
        from_token: get_token_identifier(&ctx, &token_id).await?,
        swappable_tokens,
    }))
}

#[ocean_endpoint]
async fn list_paths(
    Path((token_id, to_token_id)): Path<(String, String)>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<SwapPathsResponse>> {
    let paths = get_all_swap_paths(&ctx, &token_id, &to_token_id).await?;
    Ok(Response::new(paths))
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SwapPathsResponse {
    from_token: TokenIdentifier,
    to_token: TokenIdentifier,
    paths: Vec<Vec<SwapPathPoolPair>>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct BestSwapPathResponse {
    from_token: TokenIdentifier,
    to_token: TokenIdentifier,
    best_path: Vec<SwapPathPoolPair>,
    estimated_return: String,
    estimated_return_less_dex_fees: String,
}

#[ocean_endpoint]
async fn get_best_path(
    Path((from_token_id, to_token_id)): Path<(String, String)>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<BestSwapPathResponse>> {
    let SwapPathsResponse {
        from_token,
        to_token,
        paths,
    } = get_all_swap_paths(&ctx, &from_token_id, &to_token_id).await?;

    let mut best_path= Vec::<SwapPathPoolPair>::new();
    let mut best_return = dec!(0);
    let mut best_return_less_dex_fees = dec!(0);

    for path in paths {
        let path_len = path.len();
        let EstimatedLessDexFeeInfo {
            estimated_return,
            estimated_return_less_dex_fees,
        } = compute_return_less_dex_fees_in_destination_token(&path, &from_token_id).await?;

        if path_len == 1 {
            return Ok(Response::new(BestSwapPathResponse{
                from_token,
                to_token,
                best_path: path,
                estimated_return: format_number(estimated_return),
                estimated_return_less_dex_fees: format_number(estimated_return_less_dex_fees),
            }))
        };

        if estimated_return > best_return {
            best_return = estimated_return;
        }

        if estimated_return_less_dex_fees > best_return_less_dex_fees {
            best_return_less_dex_fees = estimated_return_less_dex_fees;
            best_path = path;
        };
    }

    Ok(Response::new(BestSwapPathResponse{
        from_token,
        to_token,
        best_path,
        estimated_return: format_number(best_return.ceil()),
        estimated_return_less_dex_fees: format_number(best_return_less_dex_fees.ceil()),
    }))
}

async fn get_all_swap_paths(ctx: &Arc<AppContext>, from_token_id: &String, to_token_id: &String) -> Result<SwapPathsResponse> {
    if from_token_id == to_token_id {
        return Err(Error::Other(format_err!("Invalid tokens: fromToken must be different from toToken")))
    }

    let mut res = SwapPathsResponse {
        from_token: get_token_identifier(ctx, from_token_id).await?,
        to_token: get_token_identifier(ctx, to_token_id).await?,
        paths: vec![],
    };

    if !ctx.services.token_graph.lock().contains_node(from_token_id.parse::<u32>()?)
        || !ctx.services.token_graph.lock().contains_node(to_token_id.parse::<u32>()?) {
            return Ok(res)
        }

    res.paths = compute_paths_between_tokens(ctx, from_token_id, to_token_id).await?;

    Ok(res)
}

// #[ocean_endpoint]
// async fn list_dex_prices(Query(DexPrices { denomination }): Query<DexPrices>) -> String {
//     format!("List of DEX prices with denomination {:?}", denomination)
// }

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/", get(list_poolpairs))
        .route("/:id", get(get_poolpair))
        .route("/:id/swaps", get(list_pool_swaps))
        .route("/:id/swaps/verbose", get(list_pool_swaps_verbose))
        .route("/paths/from/:fromTokenId/to/:toTokenId", get(list_paths))
        .route("/paths/best/from/:fromTokenId/to/:toTokenId", get(get_best_path))
        // .route(
        //     "/:id/swaps/aggregate/:interval",
        //     get(list_pool_swap_aggregates),
        // )
        .route("/paths/swappable/:tokenId", get(get_swappable_tokens))
        // .route("/dexprices", get(list_dex_prices))
        .layer(Extension(ctx))
}
