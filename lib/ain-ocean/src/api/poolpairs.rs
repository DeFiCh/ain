use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Json, Router};
use bitcoin::hex::parse;
use defichain_rpc::{
    json::poolpair::{PoolPairInfo, PoolPairsResult},
    RpcApi,
};
use log::debug;
use serde::{Deserialize, Serialize};
use serde_json::json;

use super::{
    common::{parse_dat_symbol, parse_display_symbol},
    path::Path,
    query::{PaginationQuery, Query},
    response::{ApiPagedResponse, Response},
    AppContext,
};

use crate::{
    error::ApiError,
    model::{BlockContext, PoolSwap},
    repository::RepositoryOps,
    Result,
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

// #[derive(Deserialize)]
// struct SwappableTokens {
//     token_id: String,
// }

// #[derive(Deserialize)]
// struct BestPath {
//     from_token_id: String,
//     to_token_id: String,
// }

// #[derive(Debug, Deserialize)]
// struct DexPrices {
//     denomination: Option<String>,
// }

// #[derive(Serialize, Deserialize, Debug, Clone)]
// #[serde(rename_all = "camelCase")]
// pub struct PoolSwapFromToData {
//     address: String,
//     amount: String,
//     // symbol: String,
//     // display_symbol: String,
// }

// #[derive(Serialize, Deserialize, Debug, Clone)]
// #[serde(rename_all = "camelCase")]
// pub struct PoolSwapData {
//     id: String,
//     sort: String,
//     txid: String,
//     txno: usize,
//     pool_pair_id: String,
//     from_amount: String,
//     from_token_id: u64,
//     block: BlockContext,
//     from: PoolSwapFromToData,
//     to: PoolSwapFromToData,
// }

// impl From<PoolSwap> for PoolSwapData {
//     fn from(v: PoolSwap) -> Self {
//         Self {
//             id: v.id,
//             sort: v.sort,
//             txid: v.txid.to_string(),
//             txno: v.txno,
//             pool_pair_id: v.pool_id.to_string(),
//             from_amount: v.from_amount.to_string(),
//             from_token_id: v.from_token_id,
//             from: PoolSwapFromToData {
//                 address: v.from.to_hex_string(),
//                 amount: v.from_amount.to_string(),
//                 // symbol: todo!(),
//                 // display_symbol: todo!(),
//             },
//             to: PoolSwapFromToData {
//                 address: v.to.to_hex_string(),
//                 amount: v.to_amount.to_string(),
//                 // symbol: todo!(),
//                 // display_symbol: todo!(),
//             },
//             block: v.block,
//         }
//     }
// }

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
                ab: p.reserve_a_reserve_b,
                ba: p.reserve_b_reserve_a,
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
        .map(|(k, v)| PoolPairResponse::from_with_id(k, v))
        .filter(|p| !p.symbol.starts_with("BURN-"))
        .collect::<Vec<_>>();

    Ok(ApiPagedResponse::of(res, query.size, |poolpair| {
        poolpair.id.clone()
    }))
}

// #[ocean_endpoint]
// async fn get_poolpair(Path(PoolPair { id }): Path<PoolPair>) -> String {
//     format!("Details of poolpair with id {}", id)
// }

// // Use single method for now since additional verbose keys are indexed
// // TODO: assess need for additional verbose method
// #[ocean_endpoint]
// async fn list_pool_swaps(
//     Path(id): Path<u32>,
//     Query(query): Query<PaginationQuery>,
// ) -> Result<Json<ApiPagedResponse<PoolSwapData>>> {
//     debug!("list_pool_swaps for id {id}",);
//     let next = query
//         .next
//         .map(|q| {
//             let parts: Vec<&str> = q.split('-').collect();
//             if parts.len() != 2 {
//                 return Err("Invalid query format");
//             }

//             let height = parts[0].parse::<u32>().map_err(|_| "Invalid height")?;
//             let txno = parts[1].parse::<usize>().map_err(|_| "Invalid txno")?;

//             Ok((height, txno))
//         })
//         .transpose()?
//         .unwrap_or_default();

//     debug!("next : {:?}", next);

//     let size = if query.size > 0 && query.size < 20 {
//         query.size
//     } else {
//         20
//     };

//     let swaps = services
//         .pool
//         .by_id
//         .list(Some((id, next.0, next.1)))?
//         .take(size)
//         .take_while(|item| match item {
//             Ok((k, _)) => k.0 == id,
//             _ => true,
//         })
//         .map(|item| {
//             let (_, swap) = item?;
//             Ok(PoolSwapData::from(swap))
//         })
//         .collect::<Result<Vec<_>>>()?;

//     Ok(Json(ApiPagedResponse::of(swaps, query.size, |swap| {
//         swap.sort.to_string()
//     })))
// }

// #[ocean_endpoint]
// async fn list_pool_swap_aggregates(
//     Path(SwapAggregate { id, interval }): Path<SwapAggregate>,
// ) -> String {
//     format!(
//         "Aggregate swaps for poolpair {} over interval {}",
//         id, interval
//     )
// }

// #[ocean_endpoint]
// async fn get_swappable_tokens(Path(SwappableTokens { token_id }): Path<SwappableTokens>) -> String {
//     format!("Swappable tokens for token id {}", token_id)
// }

// #[ocean_endpoint]
// async fn get_best_path(
//     Query(BestPath {
//         from_token_id,
//         to_token_id,
//     }): Query<BestPath>,
// ) -> String {
//     format!(
//         "Best path from token id {} to {}",
//         from_token_id, to_token_id
//     )
// }

// #[ocean_endpoint]
// async fn get_all_paths(
//     Query(BestPath {
//         from_token_id,
//         to_token_id,
//     }): Query<BestPath>,
// ) -> String {
//     format!(
//         "All paths from token id {} to {}",
//         from_token_id, to_token_id
//     )
// }

// #[ocean_endpoint]
// async fn list_dex_prices(Query(DexPrices { denomination }): Query<DexPrices>) -> String {
//     format!("List of DEX prices with denomination {:?}", denomination)
// }

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/", get(list_poolpairs))
        // .route("/:id", get(get_poolpair))
        // .route("/:id/swaps", get(list_pool_swaps))
        // .route("/:id/swaps/verbose", get(list_pool_swaps))
        // .route(
        //     "/:id/swaps/aggregate/:interval",
        //     get(list_pool_swap_aggregates),
        // )
        // .route("/paths/swappable/:tokenId", get(get_swappable_tokens))
        // .route(
        //     "/paths/best/from/:fromTokenId/to/:toTokenId",
        //     get(get_best_path),
        // )
        // .route("/paths/from/:fromTokenId/to/:toTokenId", get(get_all_paths))
        // .route("/dexprices", get(list_dex_prices))
        .layer(Extension(ctx))
}
