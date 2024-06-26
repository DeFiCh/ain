use std::sync::Arc;

use super::{
    common::address_to_hid,
    path::Path,
    query::{PaginationQuery, Query},
    response::ApiPagedResponse,
    AppContext,
};
use crate::{
    error::ApiError,
    model::{ScriptActivity, ScriptUnspent},
    repository::{RepositoryOps, SecondaryIndex},
    storage::SortOrder,
    Result,
};
use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use serde::Deserialize;

#[derive(Deserialize)]
struct Address {
    address: String,
}

// #[derive(Deserialize)]
// struct History {
//     address: String,
//     height: i64,
//     txno: i64,
// }

// async fn get_account_history(
//     Path(History {
//         address,
//         height,
//         txno,
//     }): Path<History>,
// ) -> String {
//     format!(
//         "Account history for address {}, height {}, txno {}",
//         address, height, txno
//     )
// }

// async fn list_account_history(Path(Address { address }): Path<Address>) -> String {
//     format!("List account history for address {}", address)
// }

// async fn get_balance(Path(Address { address }): Path<Address>) -> String {
//     format!("balance for address {address}")
// }

// async fn get_aggregation(Path(Address { address }): Path<Address>) -> String {
//     format!("Aggregation for address {}", address)
// }

// async fn list_token(Path(Address { address }): Path<Address>) -> String {
//     format!("List tokens for address {}", address)
// }

// async fn list_vault(Path(Address { address }): Path<Address>) -> String {
//     format!("List vaults for address {}", address)
// }

#[ocean_endpoint]
async fn list_transaction(
    Path(Address { address }): Path<Address>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<ScriptActivity>> {
    let hid = address_to_hid(&address, ctx.network.into())?;
    let repo = &ctx.services.script_activity;
    let res = repo
        .by_key
        .list(query.next, SortOrder::Descending)?
        .take(query.size)
        .take_while(|item| match item {
            Ok((k, _)) => k == &hid,
            _ => true,
        })
        .map(|el| repo.by_key.retrieve_primary_value(el))
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(res, query.size, |item| {
        format!(
            "{:?}-{:?}-{:?}-{:?}",
            item.id.0, item.id.1, item.id.2, item.id.3
        )
    }))
}

#[ocean_endpoint]
async fn list_transaction_unspent(
    Path(Address { address }): Path<Address>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<ScriptUnspent>> {
    let hid = address_to_hid(&address, ctx.network.into())?;
    let repo = &ctx.services.script_unspent;
    let res = repo
        .by_key
        .list(query.next, SortOrder::Descending)?
        .take(query.size)
        .take_while(|item| match item {
            Ok((k, _)) => k == &hid,
            _ => true,
        })
        .map(|el| repo.by_key.retrieve_primary_value(el))
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(res, query.size, |item| {
        item.sort.clone()
    }))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        // .route("/history/:height/:txno", get(get_account_history))
        // .route("/history", get(list_account_history))
        // .route("/balance", get(get_balance))
        // .route("/aggregation", get(get_aggregation))
        // .route("/tokens", get(list_token))
        // .route("/vaults", get(list_vault))
        .route("/:address/transactions", get(list_transaction))
        .route(
            "/:address/transactions/unspent",
            get(list_transaction_unspent),
        )
        .layer(Extension(ctx))
}
