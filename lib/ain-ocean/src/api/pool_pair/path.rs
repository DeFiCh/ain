use std::{collections::HashSet, str::FromStr, sync::Arc, time::Duration};

use defichain_rpc::json::poolpair::PoolPairInfo;
use rust_decimal::{prelude::FromPrimitive, Decimal, RoundingStrategy};
use rust_decimal_macros::dec;
use serde::Serialize;
use snafu::OptionExt;

use super::AppContext;
use crate::{
    api::{
        cache::{get_pool_pair_cached, get_token_cached, list_pool_pairs_cached},
        common::{format_number, parse_dat_symbol},
    },
    error::{
        ArithmeticOverflowSnafu, ArithmeticUnderflowSnafu, NotFoundKind, NotFoundSnafu, OtherSnafu,
    },
    network::Network,
    Error, Result, TokenIdentifier,
};

enum TokenDirection {
    In,
    Out,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct PriceRatio {
    ab: String,
    ba: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
struct EstimatedDexFeesInPct {
    ab: String,
    ba: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SwapPathPoolPair {
    pool_pair_id: String,
    symbol: String,
    token_a: TokenIdentifier,
    token_b: TokenIdentifier,
    price_ratio: PriceRatio,
    commission_fee_in_pct: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    estimated_dex_fees_in_pct: Option<EstimatedDexFeesInPct>,
}

#[derive(Debug)]
pub struct EstimatedLessDexFeeInfo {
    pub estimated_return: Decimal,
    pub estimated_return_less_dex_fees: Decimal,
}

#[derive(Debug)]
struct StackSet {
    set: HashSet<u32>,
    stack: Vec<u32>,
    size: usize,
}

impl StackSet {
    fn new() -> Self {
        Self {
            set: HashSet::new(),
            stack: Vec::new(),
            size: 0,
        }
    }

    fn has(&self, value: &u32) -> bool {
        self.set.contains(value)
    }

    fn push(&mut self, value: u32) {
        self.stack.push(value);
        self.set.insert(value);
        self.size += 1;
    }

    fn pop(&mut self) {
        if let Some(value) = self.stack.pop() {
            self.set.remove(&value);
            self.size -= 1;
        }
    }

    fn path(&self, value: u32) -> Vec<u32> {
        let mut path = self.stack.clone();
        path.push(value);
        path
    }

    fn of(value: u32, is_cycle: bool) -> Self {
        let mut set = Self::new();
        if !is_cycle {
            set.push(value);
        } else {
            set.stack.push(value);
        }
        set
    }
}

pub async fn get_token_identifier(ctx: &Arc<AppContext>, id: &str) -> Result<TokenIdentifier> {
    let (id, token) = get_token_cached(ctx, id).await?.context(NotFoundSnafu {
        kind: NotFoundKind::Token { id: id.to_string() },
    })?;
    Ok(TokenIdentifier {
        id,
        display_symbol: parse_dat_symbol(&token.symbol),
        name: token.name,
        symbol: token.symbol,
    })
}

pub async fn get_all_swap_paths(
    ctx: &Arc<AppContext>,
    from_token_id: &String,
    to_token_id: &String,
) -> Result<SwapPathsResponse> {
    sync_token_graph_if_empty(ctx).await?;

    if from_token_id == to_token_id {
        return Err(Error::Other {
            msg: "Invalid tokens: fromToken must be different from toToken".to_string(),
        });
    }

    let mut res = SwapPathsResponse {
        from_token: get_token_identifier(ctx, from_token_id).await?,
        to_token: get_token_identifier(ctx, to_token_id).await?,
        paths: vec![],
    };

    if !ctx
        .services
        .token_graph
        .lock()
        .contains_node(from_token_id.parse::<u32>()?)
        || !ctx
            .services
            .token_graph
            .lock()
            .contains_node(to_token_id.parse::<u32>()?)
    {
        return Ok(res);
    }

    res.paths = compute_paths_between_tokens(ctx, from_token_id, to_token_id).await?;

    Ok(res)
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SwapPathsResponse {
    pub from_token: TokenIdentifier,
    pub to_token: TokenIdentifier,
    pub paths: Vec<Vec<SwapPathPoolPair>>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct BestSwapPathResponse {
    pub from_token: TokenIdentifier,
    pub to_token: TokenIdentifier,
    pub best_path: Vec<SwapPathPoolPair>,
    #[serde(with = "rust_decimal::serde::str")]
    pub estimated_return: Decimal,
    #[serde(with = "rust_decimal::serde::str")]
    pub estimated_return_less_dex_fees: Decimal,
}

pub async fn get_best_path(
    ctx: &Arc<AppContext>,
    from_token_id: &String,
    to_token_id: &String,
) -> Result<BestSwapPathResponse> {
    let SwapPathsResponse {
        from_token,
        to_token,
        paths,
    } = get_all_swap_paths(ctx, from_token_id, to_token_id).await?;

    let mut best_path = Vec::<SwapPathPoolPair>::new();
    let mut best_return = dec!(0);
    let mut best_return_less_dex_fees = dec!(0);

    for path in paths {
        let path_len = path.len();
        let EstimatedLessDexFeeInfo {
            estimated_return,
            estimated_return_less_dex_fees,
        } = compute_return_less_dex_fees_in_destination_token(&path, from_token_id).await?;

        if path_len == 1 {
            return Ok(BestSwapPathResponse {
                from_token,
                to_token,
                best_path: path,
                estimated_return: estimated_return
                    .round_dp_with_strategy(8, RoundingStrategy::AwayFromZero),
                estimated_return_less_dex_fees: estimated_return_less_dex_fees
                    .round_dp_with_strategy(8, RoundingStrategy::AwayFromZero),
            });
        };

        if estimated_return > best_return {
            best_return = estimated_return;
        }

        if estimated_return_less_dex_fees > best_return_less_dex_fees {
            best_return_less_dex_fees = estimated_return_less_dex_fees;
            best_path = path;
        };
    }

    Ok(BestSwapPathResponse {
        from_token,
        to_token,
        best_path,
        estimated_return: best_return.round_dp_with_strategy(8, RoundingStrategy::AwayFromZero),
        estimated_return_less_dex_fees: best_return_less_dex_fees
            .round_dp_with_strategy(8, RoundingStrategy::AwayFromZero),
    })
}

fn all_simple_paths(
    ctx: &Arc<AppContext>,
    from_token_id: &str,
    to_token_id: &str,
) -> Result<Vec<Vec<u32>>> {
    let from_token_id = from_token_id.parse::<u32>()?;
    let to_token_id = to_token_id.parse::<u32>()?;

    let graph = &ctx.services.token_graph;
    if !graph.lock().contains_node(from_token_id) {
        return Err(Error::Other {
            msg: format!("from_token_id not found: {from_token_id:?}"),
        });
    }
    if !graph.lock().contains_node(to_token_id) {
        return Err(Error::Other {
            msg: format!("to_token_id not found: {to_token_id:?}"),
        });
    }

    let is_cycle = from_token_id == to_token_id;

    let mut stack = vec![graph
        .lock()
        .neighbors_directed(from_token_id, petgraph::Direction::Outgoing)
        .collect::<Vec<_>>()];
    let mut visited = StackSet::of(from_token_id, is_cycle);

    let mut paths: Vec<Vec<u32>> = Vec::new();
    while !stack.is_empty() {
        let child = stack.last_mut().and_then(std::vec::Vec::pop);
        if let Some(child) = child {
            if visited.has(&child) {
                continue;
            }
            if child == to_token_id {
                let mut p = visited.path(child);
                if is_cycle {
                    p[0] = from_token_id;
                }
                paths.push(p);
            }
            visited.push(child);
            if !visited.has(&to_token_id) {
                stack.push(
                    graph
                        .lock()
                        .neighbors_directed(child, petgraph::Direction::Outgoing)
                        .collect::<Vec<_>>(),
                );
            } else {
                visited.pop();
            }
        } else {
            stack.pop();
            visited.pop();
        }
    }

    Ok(paths)
}

fn get_dex_fees_pct(
    pool_pair_info: PoolPairInfo,
    from_token_id: &String,
    to_token_id: &String,
) -> Option<EstimatedDexFeesInPct> {
    let PoolPairInfo {
        id_token_a,
        id_token_b,
        dex_fee_in_pct_token_a,
        dex_fee_out_pct_token_a,
        dex_fee_in_pct_token_b,
        dex_fee_out_pct_token_b,
        ..
    } = pool_pair_info;

    let token_a_direction = if id_token_a == *from_token_id {
        TokenDirection::In
    } else {
        TokenDirection::Out
    };

    let token_b_direction = if id_token_b == *to_token_id {
        TokenDirection::Out
    } else {
        TokenDirection::In
    };

    if dex_fee_in_pct_token_a.is_none()
        && dex_fee_out_pct_token_a.is_none()
        && dex_fee_in_pct_token_b.is_none()
        && dex_fee_out_pct_token_b.is_none()
    {
        return None;
    }

    Some(EstimatedDexFeesInPct {
        ba: match token_a_direction {
            TokenDirection::In => format!("{:.8}", dex_fee_in_pct_token_a.unwrap_or_default()),
            TokenDirection::Out => format!("{:.8}", dex_fee_out_pct_token_a.unwrap_or_default()),
        },
        ab: match token_b_direction {
            TokenDirection::In => format!("{:.8}", dex_fee_in_pct_token_b.unwrap_or_default()),
            TokenDirection::Out => format!("{:.8}", dex_fee_out_pct_token_b.unwrap_or_default()),
        },
    })
}

pub async fn compute_paths_between_tokens(
    ctx: &Arc<AppContext>,
    from_token_id: &String,
    to_token_id: &String,
) -> Result<Vec<Vec<SwapPathPoolPair>>> {
    let mut pool_pair_paths = Vec::new();

    let graph = &ctx.services.token_graph;

    let paths = all_simple_paths(ctx, from_token_id, to_token_id)?;

    for path in paths {
        if path.len() > 4 {
            continue;
        }

        let mut pool_pairs = Vec::new();

        for i in 1..path.len() {
            let token_a = path[i - 1];
            let token_b = path[i];

            let pool_pair_id = graph
                .lock()
                .edge_weight(token_a, token_b)
                .context(OtherSnafu {
                    msg: format!(
                        "Unexpected error encountered during path finding - could not find edge between {token_a} and {token_b}"
                    )
                })?
                .to_string();

            let Some((_, pool_pair_info)) = get_pool_pair_cached(ctx, pool_pair_id.clone()).await?
            else {
                return Err(Error::Other {
                    msg: format!("Pool pair by id {pool_pair_id} not found"),
                });
            };

            let estimated_dex_fees_in_pct =
                get_dex_fees_pct(pool_pair_info.clone(), from_token_id, to_token_id);

            let PoolPairInfo {
                symbol,
                id_token_a,
                id_token_b,
                reserve_a_reserve_b: ab,
                reserve_b_reserve_a: ba,
                commission,
                ..
            } = pool_pair_info;

            let swap_path_pool_pair = SwapPathPoolPair {
                pool_pair_id,
                symbol,
                token_a: get_token_identifier(ctx, &id_token_a).await?,
                token_b: get_token_identifier(ctx, &id_token_b).await?,
                price_ratio: PriceRatio {
                    ab: format_number(Decimal::from_f64(ab).unwrap_or_default()),
                    ba: format_number(Decimal::from_f64(ba).unwrap_or_default()),
                },
                commission_fee_in_pct: format_number(
                    Decimal::from_f64(commission).unwrap_or_default(),
                ),
                estimated_dex_fees_in_pct,
            };

            pool_pairs.push(swap_path_pool_pair);
        }

        pool_pair_paths.push(pool_pairs);
    }

    Ok(pool_pair_paths)
}

pub async fn compute_return_less_dex_fees_in_destination_token(
    path: &Vec<SwapPathPoolPair>,
    from_token_id: &String,
) -> Result<EstimatedLessDexFeeInfo> {
    let mut estimated_return_less_dex_fees = dec!(1);
    let mut estimated_return = dec!(1);

    let mut from_token_id = from_token_id.to_owned();
    let mut price_ratio;
    let mut from_token_fee_pct;
    let mut to_token_fee_pct;

    for pool in path {
        if from_token_id == pool.token_a.id {
            pool.token_b.id.clone_into(&mut from_token_id);
            price_ratio = Decimal::from_str(pool.price_ratio.ba.as_str())?;
            (from_token_fee_pct, to_token_fee_pct) =
                if let Some(estimated_dex_fees_in_pct) = &pool.estimated_dex_fees_in_pct {
                    let ba = Decimal::from_str(estimated_dex_fees_in_pct.ba.as_str())?;
                    let ab = Decimal::from_str(estimated_dex_fees_in_pct.ab.as_str())?;
                    (Some(ba), Some(ab))
                } else {
                    (None, None)
                };
        } else {
            pool.token_a.id.clone_into(&mut from_token_id);
            price_ratio = Decimal::from_str(pool.price_ratio.ab.as_str())?;
            (from_token_fee_pct, to_token_fee_pct) =
                if let Some(estimated_dex_fees_in_pct) = &pool.estimated_dex_fees_in_pct {
                    let ab = Decimal::from_str(estimated_dex_fees_in_pct.ab.as_str())?;
                    let ba = Decimal::from_str(estimated_dex_fees_in_pct.ba.as_str())?;
                    (Some(ab), Some(ba))
                } else {
                    (None, None)
                };
        };

        estimated_return = estimated_return
            .checked_mul(price_ratio)
            .context(ArithmeticOverflowSnafu)?;

        // less commission fee
        let commission_fee_in_pct = Decimal::from_str(pool.commission_fee_in_pct.as_str())?;
        let commission_fee = estimated_return_less_dex_fees
            .checked_mul(commission_fee_in_pct)
            .context(ArithmeticOverflowSnafu)?;
        estimated_return_less_dex_fees = estimated_return_less_dex_fees
            .checked_sub(commission_fee)
            .context(ArithmeticUnderflowSnafu)?;

        // less dex fee from_token
        let from_token_estimated_dex_fee = from_token_fee_pct
            .unwrap_or_default()
            .checked_mul(estimated_return_less_dex_fees)
            .context(ArithmeticOverflowSnafu)?;

        estimated_return_less_dex_fees = estimated_return_less_dex_fees
            .checked_sub(from_token_estimated_dex_fee)
            .context(ArithmeticUnderflowSnafu)?;

        // convert to to_token
        let from_token_estimated_return_less_dex_fee = estimated_return_less_dex_fees
            .checked_mul(price_ratio)
            .context(ArithmeticOverflowSnafu)?;
        let to_token_estimated_dex_fee = to_token_fee_pct
            .unwrap_or_default()
            .checked_mul(from_token_estimated_return_less_dex_fee)
            .context(ArithmeticOverflowSnafu)?;

        // less dex fee to_token
        estimated_return_less_dex_fees = from_token_estimated_return_less_dex_fee
            .checked_sub(to_token_estimated_dex_fee)
            .context(ArithmeticUnderflowSnafu)?;
    }

    Ok(EstimatedLessDexFeeInfo {
        estimated_return,
        estimated_return_less_dex_fees,
    })
}

pub async fn sync_token_graph(ctx: &Arc<AppContext>) -> Result<()> {
    let mut interval = tokio::time::interval(Duration::from_secs(120));

    loop {
        let pools = list_pool_pairs_cached(ctx, None, None).await?;

        // addTokensAndConnectionsToGraph
        for (k, v) in pools.0 {
            // isPoolPairIgnored
            if !v.status {
                continue;
            }
            // skip mainnet BURN-DFI pool
            if ctx.network == Network::Mainnet && k == "48" {
                continue;
            }
            let id_token_a = v.id_token_a.parse::<u32>()?;
            let id_token_b = v.id_token_b.parse::<u32>()?;
            let graph = &ctx.services.token_graph;
            if !graph.lock().contains_node(id_token_a) {
                graph.lock().add_node(id_token_a);
            }
            if !graph.lock().contains_node(id_token_b) {
                graph.lock().add_node(id_token_b);
            }
            if !graph.lock().contains_edge(id_token_a, id_token_b) {
                graph.lock().add_edge(id_token_a, id_token_b, k);
            }
        }

        // wait 120s
        interval.tick().await;
    } // end of loop
}

pub async fn sync_token_graph_if_empty(ctx: &Arc<AppContext>) -> Result<()> {
    if ctx.services.token_graph.lock().edge_count() == 0 {
        let ctx_cloned = ctx.clone();
        tokio::spawn(async move { sync_token_graph(&ctx_cloned).await });
        return Ok(());
    };
    Ok(())
}
