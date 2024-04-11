
use std::{collections::HashSet, sync::Arc, time::Duration};

use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use bitcoin::hex::parse;
use defichain_rpc::{
    json::poolpair::{PoolPairInfo, PoolPairsResult},
    json::token::TokenInfo,
    RpcApi,
};
use json::from;
use log::debug;
use petgraph::visit::{IntoNeighborsDirected, VisitMap, Visitable};
use serde::{Deserialize, Serialize};
use serde_json::json;
use anyhow::format_err;

use super::{
    cache::{get_token_cached, get_pool_pair_info_cached, list_pool_pairs_cached},
    common::parse_dat_symbol,
    path::Path,
    query::{PaginationQuery, Query},
    response::{ApiPagedResponse, Response},
    AppContext,
};

use crate::{
    error::ApiError,
    model::{BlockContext, PoolSwap},
    repository::{InitialKeyProvider, PoolSwapRepository, RepositoryOps},
    storage::SortOrder,
    Result, TokenIdentifier,
};

// #[derive(Debug, Clone)]
// struct PriceRatio {
//     ab: BigDecimal,
//     ba: BigDecimal,
// }

#[derive(Debug, Clone)]
struct SwapPathPoolPair {
    id: String,
    symbol: String,
    token_a: String,
    token_b: String,
    // price_ratio: PriceRatio,
    // commission_fee_in_pct: BigDecimal,
    // estimated_dex_fees_in_pct: Option<BigDecimal>,
}

#[derive(Debug)]
struct StackSet {
    set: HashSet<String>,
    stack: Vec<String>,
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

    fn has(&self, value: &String) -> bool {
        self.set.contains(value)
    }

    fn push(&mut self, value: String) {
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

    fn path(&self, value: String) -> Vec<String> {
        let mut path = self.stack.clone();
        path.push(value);
        path
    }

    fn of(value: String) -> Self {
        let mut set = StackSet::new();
        set.push(value);
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

fn all_simple_paths(ctx: &Arc<AppContext>, mut from_token_id: String, mut to_token_id: String) -> Result<Vec<Vec<String>>> {
    let graph = ctx.services.token_graph;
    let from_token_id_u32 = from_token_id.parse::<u32>()?;
    let to_token_id_u32 = to_token_id.parse::<u32>()?;
    if !graph.lock().contains_node(from_token_id_u32) {
        return Err(format_err!("from_token_id not found: {:?}", from_token_id_u32).into())
    }
    if !graph.lock().contains_node(to_token_id_u32) {
        return Err(format_err!("to_token_id not found: {:?}", to_token_id_u32).into())
    }

    from_token_id = "" + from_token_id.to_string();
    to_token_id = "" + to_token_id.to_string();

    let is_cycle = from_token_id == to_token_id;

    let mut stack = vec![graph.lock().neighbors_directed(&from_token_id, petgraph::Direction::Outgoing)];
    let mut visited;

    if cycle {
        visited = StackSet::of("§SOURCE§".to_string());
    } else {
        visited = StackSet::of(from_token_id.clone());
    }

    let mut paths: Vec<Vec<String>> = Vec::new();
    let mut children: Vec<String>;
    let mut child: Option<String>;
    while stack.len() != 0 {
        children = stack.last_mut().unwrap();
        child = children.pop();
        if let Some(child) = child {
            if visited.contains(&child) {
                continue;
            }
            if child == to_token_id {
                let mut p = visited.path(child);
                if cycle {
                    p[0] = from_token_id.clone();
                }
                paths.push(p);
            }
            visited.push(child.clone());
            let child_u32 = child.clone().parse::<u32>()?;
            if !visited.has(&from_token_id) {
                stack.push(graph.lock().neighbors_directed(&child_u32, petgraph::Direction::Outgoing))
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

pub async fn compute_paths_between_tokens(ctx: &Arc<AppContext>, from_token_id: String, to_token_id: String) -> Result<Vec<Vec<SwapPathPoolPair>>> {
    let mut pool_pair_paths = Vec::new();

    let graph = ctx.services.token_graph;

    for path in all_simple_paths(ctx, from_token_id, to_token_id) {
        if path.len() > 4 {
            continue;
        }

        let mut pool_pairs = Vec::new();

        for i in 1..path.len() {
            let token_a = &path[i - 1];
            let token_b = &path[i];

            let pool_pair_id = graph.lock().add_edge(token_a, token_b, ()).ok_or_else(|| {
                format_err!(
                    "Unexpected error encountered during path finding - could not find edge between {} and {}",
                    token_a,
                    token_b
                )
            })?;

            let pool_pair = get_pool_pair_info_cached(&ctx, id).await?;
            // let estimated_dex_fees_in_pct

            let swap_path_pool_pair = SwapPathPoolPair {
                id: pool_pair_id,
                symbol: pool_pair.symbol,
                token_a: get_token_identifier(&ctx, pool_pair.id_token_a).await?,
                token_b: get_token_identifier(&ctx, pool_pair.id_token_b).await?,
                // commission_fee_in_pct: todo!(),
                // estimated_dex_fees_in_pct: todo!(),
            };

            pool_pairs.push(swap_path_pool_pair);
        }

        pool_pair_paths.push(pool_pairs);
    }

    Ok(pool_pair_paths)
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
      // wait 120s
      interval.tick().await;
      // then
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
              graph.lock().add_edge(id_token_a, id_token_b, ());
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
  } // end of loop
}