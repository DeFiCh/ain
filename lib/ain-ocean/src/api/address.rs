use std::sync::Arc;

use super::{
    common::address_to_hid,
    path::Path,
    query::{PaginationQuery, Query},
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    error::ApiError,
    model::{BlockContext, ScriptActivity, ScriptAggregation, ScriptUnspent},
    repository::{RepositoryOps, SecondaryIndex},
    storage::SortOrder,
    Result,
};
use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use bitcoin::hex::DisplayHex;
use serde::{Serialize, Deserialize};

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

#[derive(Debug, Serialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct ScriptAggregationResponse {
    pub id: String,
    pub hid: String,
    pub block: BlockContext,
    pub script: ScriptAggregationScriptResponse,
    pub statistic: ScriptAggregationStatisticResponse,
    pub amount: ScriptAggregationAmountResponse,
}

impl From<ScriptAggregation> for ScriptAggregationResponse {
    fn from(v: ScriptAggregation) -> Self {
        Self {
            id: format!("{}{}", hex::encode(v.id.1.to_be_bytes()), v.id.0),
            hid: v.hid,
            block: v.block,
            script: ScriptAggregationScriptResponse {
                r#type: v.script.r#type,
                hex: v.script.hex.as_hex().to_string(),
            },
            statistic: ScriptAggregationStatisticResponse {
                tx_count: v.statistic.tx_count,
                tx_in_count: v.statistic.tx_in_count,
                tx_out_count: v.statistic.tx_out_count,
            },
            amount: ScriptAggregationAmountResponse {
                tx_in: v.amount.tx_in,
                tx_out: v.amount.tx_out,
                unspent: v.amount.unspent,
            }
        }
    }
}

#[derive(Debug, Serialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct ScriptAggregationScriptResponse {
    pub r#type: String,
    pub hex: String,
}

#[derive(Debug, Serialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct ScriptAggregationStatisticResponse {
    pub tx_count: i32,
    pub tx_in_count: i32,
    pub tx_out_count: i32,
}

#[derive(Debug, Serialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct ScriptAggregationAmountResponse {
    pub tx_in: String,
    pub tx_out: String,
    pub unspent: String,
}

fn get_latest_aggregation(ctx: &Arc<AppContext>, hid: String) -> Result<Option<ScriptAggregationResponse>> {
    let latest = ctx
        .services
        .script_aggregation
        .by_id
        .list(Some((hid.clone(), u32::MAX)), SortOrder::Descending)?
        .take(1)
        .take_while(|item| match item {
            Ok(((v, _), _)) => v == &hid,
            _ => true,
        })
        .map(|item| {
            let (_, v) = item?;
            let res = v.into();
            Ok(res)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(latest.first().cloned())
}

#[ocean_endpoint]
async fn get_balance(
    Path(Address { address }): Path<Address>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<String>> {
    let hid = address_to_hid(&address, ctx.network.into())?;
    let aggregation = get_latest_aggregation(&ctx, hid)?;
    if aggregation.is_none() {
        return Ok(Response::new("0".to_string()))
    }
    let aggregation = aggregation.unwrap();
    Ok(Response::new(format!("{:8}", aggregation.amount.unspent)))
}

#[ocean_endpoint]
async fn get_aggregation(
    Path(Address { address }): Path<Address>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<Option<ScriptAggregationResponse>>> {
    let hid = address_to_hid(&address, ctx.network.into())?;
    let aggregation = get_latest_aggregation(&ctx, hid)?;
    Ok(Response::new(aggregation))
}

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
        .route("/:address/balance", get(get_balance))
        .route("/:address/aggregation", get(get_aggregation))
        // .route("/tokens", get(list_token))
        // .route("/vaults", get(list_vault))
        .route("/:address/transactions", get(list_transaction))
        .route(
            "/:address/transactions/unspent",
            get(list_transaction_unspent),
        )
        .layer(Extension(ctx))
}
