
use std::{collections::HashSet, str::FromStr, sync::Arc, time::Duration};

use defichain_rpc::json::{poolpair::PoolPairInfo, token::TokenInfo};
use serde::Serialize;
use anyhow::format_err;
use rust_decimal::Decimal;
use rust_decimal_macros::dec;

use super::{
    cache::{get_token_cached, get_pool_pair_info_cached, list_pool_pairs_cached},
    common::parse_dat_symbol,
    AppContext,
};

use crate::{
    Result, TokenIdentifier,
};

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
        let mut set = StackSet::new();
        if !is_cycle {
            set.push(value);
        } else {
           set.stack.push(value);
        }
        set
    }
}

pub async fn get_token_identifier(ctx: &Arc<AppContext>, id: String) -> Result<TokenIdentifier> {
    let (id, token) = get_token_cached(ctx, &id).await?;
    Ok(TokenIdentifier{
        id,
        name: token.name,
        symbol: token.symbol.clone(),
        display_symbol: parse_dat_symbol(&token.symbol),
    })

}

fn all_simple_paths(ctx: &Arc<AppContext>, from_token_id: u32, to_token_id: u32) -> Result<Vec<Vec<u32>>> {
    let graph = &ctx.services.token_graph;
    if !graph.lock().contains_node(from_token_id) {
        return Err(format_err!("from_token_id not found: {:?}", from_token_id).into())
    }
    if !graph.lock().contains_node(to_token_id) {
        return Err(format_err!("to_token_id not found: {:?}", to_token_id).into())
    }

    let is_cycle = from_token_id == to_token_id;

    let mut stack = vec![graph.lock().neighbors_directed(from_token_id, petgraph::Direction::Outgoing).collect::<Vec<_>>()];
    let mut visited = StackSet::of(from_token_id, is_cycle);

    let mut paths: Vec<Vec<u32>> = Vec::new();
    while !stack.is_empty() {
        let child = stack.last_mut().unwrap().pop();
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
                stack.push(graph.lock().neighbors_directed(child, petgraph::Direction::Outgoing).collect::<Vec<_>>())
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

pub async fn compute_paths_between_tokens(ctx: &Arc<AppContext>, from_token_id: u32, to_token_id: u32) -> Result<Vec<Vec<SwapPathPoolPair>>> {
    let mut pool_pair_paths = Vec::new();

    let graph = &ctx.services.token_graph;

    let paths = all_simple_paths(ctx, from_token_id, to_token_id).unwrap();

    for path in  paths {
        if path.len() > 4 {
            continue;
        }

        let mut pool_pairs = Vec::new();

        for i in 1..path.len() {
            let token_a = path[i - 1];
            let token_b = path[i];

            let pool_pair_id = graph.lock().edge_weight(token_a, token_b).unwrap().to_string();
            // .ok_or_else(|| {
            //     format_err!(
            //         "Unexpected error encountered during path finding - could not find edge between {} and {}",
            //         token_a,
            //         token_b
            //     )
            // })?;

            let (_, pool_pair_info) = get_pool_pair_info_cached(&ctx, pool_pair_id.clone()).await?;

            let PoolPairInfo{
                symbol,
                id_token_a,
                id_token_b,
                reserve_a_reserve_b: ab,
                reserve_b_reserve_a: ba,
                commission,
                dex_fee_in_pct_token_a,
                dex_fee_out_pct_token_a,
                dex_fee_in_pct_token_b,
                dex_fee_out_pct_token_b,
                ..
            } = pool_pair_info;

            let token_a_direction = if id_token_a == from_token_id.to_string() {
                "in"
            } else {
                "out"
            };

            let token_b_direction = if id_token_b == to_token_id.to_string() {
                "out"
            } else {
                "in"
            };

            let estimated_dex_fees_in_pct = if let (Some(dex_fee_in_pct_token_a), Some(dex_fee_out_pct_token_a), Some(dex_fee_in_pct_token_b), Some(dex_fee_out_pct_token_b)) = (dex_fee_in_pct_token_a, dex_fee_out_pct_token_a, dex_fee_in_pct_token_b, dex_fee_out_pct_token_b) {
                Some(EstimatedDexFeesInPct{
                    ba: if token_a_direction == "in" {
                        dex_fee_in_pct_token_a.to_string()
                    } else {
                        dex_fee_out_pct_token_a.to_string()
                    },
                    ab: if token_b_direction == "in" {
                        dex_fee_in_pct_token_b.to_string()
                    } else {
                        dex_fee_out_pct_token_b.to_string()
                    },
                })
            } else {
                None
            };

            let ab = if ab == 0f64 {
                ab.to_string()
            } else {
                format!("{:.8}", ab)
            };

            let ba = if ba == 0f64 {
                ba.to_string()
            } else {
                format!("{:.8}", ba)
            };

            let commission = if commission == 0f64 {
                commission.to_string()
            } else {
                format!("{:.8}", commission)
            };

            let swap_path_pool_pair = SwapPathPoolPair {
                pool_pair_id,
                symbol,
                token_a: get_token_identifier(&ctx, id_token_a).await?,
                token_b: get_token_identifier(&ctx, id_token_b).await?,
                price_ratio: PriceRatio {
                    ab,
                    ba,
                },
                commission_fee_in_pct: commission,
                estimated_dex_fees_in_pct,
            };

            pool_pairs.push(swap_path_pool_pair);
        }

        pool_pair_paths.push(pool_pairs);
    }

    Ok(pool_pair_paths)
}

pub async fn compute_return_less_dex_fees_in_destination_token(path: &Vec<SwapPathPoolPair>, from_token_id: u32) -> Result<EstimatedLessDexFeeInfo> {
    let mut estimated_return_less_dex_fees = dec!(1);
    let mut estimated_return= dec!(1);

    let mut from_token_id = from_token_id.to_string();
    let mut price_ratio;
    let mut from_token_fee_pct;
    let mut to_token_fee_pct;

    for pool in path {
        if from_token_id == pool.token_a.id {
            from_token_id = pool.token_b.id.to_owned();
            price_ratio = Decimal::from_str(pool.price_ratio.ba.as_str()).map_err(|e| format_err!(e))?;
            (from_token_fee_pct, to_token_fee_pct) = if let Some(estimated_dex_fees_in_pct) = &pool.estimated_dex_fees_in_pct {
                let ba = Decimal::from_str(estimated_dex_fees_in_pct.ba.as_str()).map_err(|e| format_err!(e))?;
                let ab = Decimal::from_str(estimated_dex_fees_in_pct.ab.as_str()).map_err(|e| format_err!(e))?;
                (Some(ba), Some(ab))
            } else {
                (None, None)
            };
        } else {
            from_token_id = pool.token_a.id.to_owned();
            price_ratio = Decimal::from_str(pool.price_ratio.ab.as_str()).map_err(|e| format_err!(e))?;
            (from_token_fee_pct, to_token_fee_pct) = if let Some(estimated_dex_fees_in_pct) = &pool.estimated_dex_fees_in_pct {
                let ab = Decimal::from_str(estimated_dex_fees_in_pct.ab.as_str()).map_err(|e| format_err!(e))?;
                let ba = Decimal::from_str(estimated_dex_fees_in_pct.ba.as_str()).map_err(|e| format_err!(e))?;
                (Some(ab), Some(ba))
            } else {
                (None, None)
            };
        };

        estimated_return = estimated_return.checked_mul(price_ratio).ok_or_else(|| format_err!("estimated_return overflow"))?;

        // less commission fee
        let commission_fee_in_pct = Decimal::from_str(pool.commission_fee_in_pct.as_str()).map_err(|e| format_err!(e))?;
        let commission_fee = estimated_return_less_dex_fees.checked_mul(commission_fee_in_pct).ok_or_else(|| format_err!("commission_fee overflow"))?;
        estimated_return_less_dex_fees = estimated_return_less_dex_fees.checked_sub(commission_fee).ok_or_else(|| format_err!("estimated_return_less_dex_fees underflow"))?;

        // less dex fee from_token
        let from_token_estimated_dex_fee = if let Some(from_token_fee_pct) = from_token_fee_pct {
            from_token_fee_pct.checked_mul(estimated_return_less_dex_fees).ok_or_else(|| format_err!("from_token_fee_pct overflow"))?
        } else {
            dec!(0)
        };
        estimated_return_less_dex_fees = estimated_return_less_dex_fees.checked_sub(from_token_estimated_dex_fee).ok_or_else(|| format_err!("estimated_return_less_dex_fees underflow"))?;

        // convert to to_token
        let from_token_estimated_return_less_dex_fee = estimated_return_less_dex_fees.checked_mul(price_ratio).ok_or_else(|| format_err!("from_token_estimated_return_less_dex_fee overflow"))?;
        let to_token_estimated_dex_fee = if let Some(to_token_fee_pct) = to_token_fee_pct {
            to_token_fee_pct.checked_mul(from_token_estimated_return_less_dex_fee).ok_or_else(|| format_err!("to_token_estimated_dex_fee overflow"))?
        } else {
            dec!(0)
        };

        // less dex fee to_token
        estimated_return_less_dex_fees = from_token_estimated_return_less_dex_fee.checked_sub(to_token_estimated_dex_fee).ok_or_else(|| format_err!("estimated_return_less_dex_fees underflow"))?;

    }
    Ok(EstimatedLessDexFeeInfo{
        estimated_return: estimated_return.ceil(),
        estimated_return_less_dex_fees: estimated_return_less_dex_fees.ceil(),
    })
}

fn to_token_identifier(id: &String, info: &TokenInfo) -> TokenIdentifier {
    TokenIdentifier {
        id: id.to_owned(),
        name: info.name.to_owned(),
        symbol: info.symbol.to_owned(),
        display_symbol: parse_dat_symbol(info.symbol.as_str()),
    }
}

pub async fn sync_token_graph(ctx: &Arc<AppContext>) {
  let mut interval = tokio::time::interval(Duration::from_secs(120));

  loop {
      let pools = list_pool_pairs_cached(ctx).await.unwrap();

      // addTokensAndConnectionsToGraph
      for (k, v) in pools.0 {
          // isPoolPairIgnored
          if !v.status {
              continue;
          }
          if ctx.network == "mainnet" && k == "48" {
              continue;
          }
          let id_token_a = v.id_token_a.parse::<u32>().unwrap();
          let id_token_b = v.id_token_b.parse::<u32>().unwrap();
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

      // updateTokensToSwappableTokens
      let mut token_identifiers = vec![];
      let token_ids = &ctx.services.token_graph.lock().nodes().collect::<Vec<_>>();
      for id in token_ids {
          let (id, token) = get_token_cached(ctx, id.to_string().as_str()).await.unwrap();
          let token_identifier = to_token_identifier(&id, &token);
          token_identifiers.push(token_identifier);
      }

      let token_identifiers_cloned = token_identifiers.clone();

      // index each token to their swappable tokens
      for token_identifier in token_identifiers {
          ctx
              .services
              .tokens_to_swappable_tokens
              .lock()
              .insert(
                  token_identifier.clone().id,
                  token_identifiers_cloned
                      .clone()
                      .into_iter()
                      .filter(|t| t.id != token_identifier.id) // exclude tokens from their own 'swappables' list
                      .collect::<Vec<_>>(),
              );
      }
      // wait 120s
      interval.tick().await;
  } // end of loop
}