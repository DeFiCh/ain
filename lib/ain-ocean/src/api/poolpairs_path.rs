
use std::{sync::Arc, time::Duration};

use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use bitcoin::hex::parse;
use defichain_rpc::{
    json::poolpair::{PoolPairInfo, PoolPairsResult},
    json::token::TokenInfo,
    RpcApi,
};
use log::debug;
use serde::{Deserialize, Serialize};
use serde_json::json;

use super::{
    cache::{get_token_cached, list_pool_pairs_cached},
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