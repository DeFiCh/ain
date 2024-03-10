use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use bitcoin::Txid;
use defichain_rpc::{
    defichain_rpc_json::{
        loan::{CollateralTokenDetail, LoanSchemeResult},
        token::TokenInfo,
    },
    LoanRPC,
};
use futures::future::try_join_all;
use log::debug;
use serde::Serialize;

use super::{
    cache::get_token_cached,
    common::Paginate,
    path::Path,
    query::{PaginationQuery, Query},
    response::{ApiPagedResponse, Response},
    tokens::TokenData,
    AppContext,
};
use crate::{
    error::{ApiError, Error},
    model::VaultAuctionBatchHistory,
    repository::RepositoryOps,
    storage::SortOrder,
    Result,
};

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LoanSchemeData {
    id: String,
    min_col_ratio: String,
    interest_rate: String,
}

impl From<LoanSchemeResult> for LoanSchemeData {
    fn from(value: LoanSchemeResult) -> Self {
        Self {
            id: value.id,
            min_col_ratio: format!("{}", value.mincolratio),
            interest_rate: format!("{}", value.interestrate),
        }
    }
}

#[ocean_endpoint]
async fn list_scheme(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<LoanSchemeData>> {
    let skip_while = |el: &LoanSchemeResult| match &query.next {
        None => false,
        Some(v) => v != &el.id,
    };

    let res = ctx
        .client
        .list_loan_schemes()
        .await?
        .into_iter()
        .fake_paginate(&query, skip_while)
        .map(Into::into)
        .collect();
    Ok(ApiPagedResponse::of(res, query.size, |loan_scheme| {
        loan_scheme.id.to_owned()
    }))
}

#[ocean_endpoint]
async fn get_scheme(
    Path(scheme_id): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<LoanSchemeData>> {
    Ok(Response::new(
        ctx.client.get_loan_scheme(scheme_id).await?.into(),
    ))
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CollateralToken {
    token_id: String,
    token: TokenData,
    factor: String,
    activate_after_block: u32,
    fixed_interval_price_id: String,
    // TODO when indexing price
    // activePrice?: ActivePrice
}

impl CollateralToken {
    fn from_with_id(id: String, detail: CollateralTokenDetail, info: TokenInfo) -> Self {
        Self {
            token_id: detail.token_id,
            factor: format!("{}", detail.factor),
            activate_after_block: 0,
            fixed_interval_price_id: detail.fixed_interval_price_id,
            token: TokenData::from_with_id(id, info),
        }
    }
}

#[ocean_endpoint]
async fn list_collateral_token(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<CollateralToken>> {
    let skip_while = |el: &CollateralTokenDetail| match &query.next {
        None => false,
        Some(v) => v != &el.token_id,
    };

    let tokens = ctx.client.list_collateral_tokens().await?;

    let fut = tokens
        .into_iter()
        .fake_paginate(&query, skip_while)
        .map(|v| async {
            let (id, info) = get_token_cached(&ctx, &v.token_id).await?;
            Ok::<CollateralToken, Error>(CollateralToken::from_with_id(id, v, info))
        })
        .collect::<Vec<_>>();

    let res = try_join_all(fut).await?;

    Ok(ApiPagedResponse::of(res, query.size, |loan_scheme| {
        loan_scheme.token_id.to_owned()
    }))
}

#[ocean_endpoint]
async fn get_collateral_token(
    Path(token_id): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<CollateralToken>> {
    let collateral_token = ctx.client.get_collateral_token(token_id).await?;
    let (id, info) = get_token_cached(&ctx, &collateral_token.token_id).await?;

    Ok(Response::new(CollateralToken::from_with_id(
        id,
        collateral_token,
        info,
    )))
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LoanToken {
    token_id: String,
    token: TokenData,
    interest: f64,
    // activate_after_block: u32,
    // fixed_interval_price_id: String,
    // TODO when indexing price
    // activePrice?: ActivePrice
}

#[ocean_endpoint]
async fn list_loan_token(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<LoanToken>> {
    let tokens = ctx.client.list_loan_tokens().await?;

    struct FlattenToken {
        id: String,
        data: TokenInfo,
        interest: f64,
    }

    let fut = tokens
        .into_iter()
        .flat_map(|el| {
            el.token
                .0
                .into_iter()
                .next() // Should always get a Hashmap<id, data> with single entry here.
                .map(|(id, data)| FlattenToken {
                    id,
                    data,
                    interest: el.interest,
                })
        })
        .fake_paginate(&query, |token| match &query.next {
            None => false,
            Some(v) => v != &token.id,
        })
        .map(|token| async move {
            let token = LoanToken {
                token_id: token.id.clone(),
                token: TokenData::from_with_id(token.id, token.data),
                interest: token.interest,
                // activate_after_block: todo!(),
                // fixed_interval_price_id: todo!(),
            };
            Ok::<LoanToken, Error>(token)
        })
        .collect::<Vec<_>>();

    let res = try_join_all(fut).await?;

    Ok(ApiPagedResponse::of(res, query.size, |loan_scheme| {
        loan_scheme.token_id.to_owned()
    }))
}

// #[ocean_endpoint]
// async fn get_loan_token(Path(token_id): Path<String>) -> String {
//     format!("Details of loan token with id {}", token_id)
// }

// #[ocean_endpoint]
// async fn list_vault() -> String {
//     "List of vaults".to_string()
// }

// #[ocean_endpoint]
// async fn get_vault(Path(vault_id): Path<String>) -> String {
//     format!("Details of vault with id {}", vault_id)
// }

#[ocean_endpoint]
async fn list_vault_auction_history(
    Path((vault_id, height, batch_index)): Path<(Txid, u32, u32)>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<VaultAuctionBatchHistory>> {
    debug!(
        "Auction history for vault id {}, height {}, batch index {}",
        vault_id, height, batch_index
    );
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

    let size = if query.size > 0 { query.size } else { 20 };

    let auctions = ctx
        .services
        .auction
        .by_height
        .list(
            Some((vault_id, batch_index, next.0, next.1)),
            SortOrder::Descending,
        )?
        .take(size)
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == vault_id && k.1 == batch_index,
            _ => true,
        })
        .map(|item| {
            let (_, id) = item?;

            let auction = ctx
                .services
                .auction
                .by_id
                .get(&id)?
                .ok_or("Missing auction index")?;

            Ok(auction)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(auctions, query.size, |auction| {
        auction.sort.to_string()
    }))
}

// async fn list_auction() -> String {
//     "List of auctions".to_string()
// }

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/schemes", get(list_scheme))
        .route("/schemes/:id", get(get_scheme))
        .route("/collaterals", get(list_collateral_token))
        .route("/collaterals/:id", get(get_collateral_token))
        .route("/tokens", get(list_loan_token))
        // .route("/tokens/:id", get(get_loan_token))
        // .route("/vaults", get(list_vault))
        // .route("/vaults/:id", get(get_vault))
        .route(
            "/vaults/:id/auctions/:height/batches/:batchIndex/history",
            get(list_vault_auction_history),
        )
        // .route("/auctions", get(list_auction))
        .layer(Extension(ctx))
}
