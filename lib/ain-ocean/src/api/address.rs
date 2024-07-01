use std::{str::FromStr, sync::Arc};

use super::{
    common::address_to_hid,
    path::Path,
    query::{PaginationQuery, Query},
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    error::ApiError,
    model::{BlockContext, ScriptActivity, ScriptActivityTypeHex, ScriptAggregation, ScriptUnspent},
    repository::RepositoryOps,
    storage::SortOrder,
    Error, Result,
};
use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use bitcoin::{hashes::Hash, hex::DisplayHex, Txid};
use serde::{Deserialize, Serialize};
use serde_with::skip_serializing_none;

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
                tx_in: format!("{:.8}", v.amount.tx_in),
                tx_out: format!("{:.8}", v.amount.tx_out),
                unspent: format!("{:.8}", v.amount.unspent),
            },
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

fn get_latest_aggregation(
    ctx: &Arc<AppContext>,
    hid: String,
) -> Result<Option<ScriptAggregationResponse>> {
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
        return Ok(Response::new("0.00000000".to_string()));
    }
    let aggregation = aggregation.unwrap();
    Ok(Response::new(aggregation.amount.unspent))
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

#[skip_serializing_none]
#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ScriptActivityResponse {
    pub id: String,
    pub hid: String,
    pub r#type: String,
    pub type_hex: String,
    pub txid: Txid,
    pub block: BlockContext,
    pub script: ScriptActivityScriptResponse,
    pub vin: Option<ScriptActivityVinVoutResponse>,
    pub vout: Option<ScriptActivityVinVoutResponse>,
    pub value: String,
    pub token_id: Option<u32>,
}

impl From<ScriptActivity> for ScriptActivityResponse {
    fn from(v: ScriptActivity) -> Self {
        Self {
            id: v.id,
            hid: v.hid,
            r#type: v.r#type.to_string(),
            type_hex: v.type_hex.to_string(),
            txid: v.txid,
            block: v.block,
            script: ScriptActivityScriptResponse {
                r#type: v.script.r#type,
                hex: v.script.hex.to_lower_hex_string(),
            },
            vin: v.vin.map(|vin| {
                ScriptActivityVinVoutResponse {
                    txid: vin.txid,
                    n: vin.n
                }
            }),
            vout: v.vout.map(|vout| {
                ScriptActivityVinVoutResponse {
                    txid: vout.txid,
                    n: vout.n
                }
            }),
            value: v.value,
            token_id: v.token_id,
        }
    }
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ScriptActivityScriptResponse {
    pub r#type: String,
    pub hex: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ScriptActivityVinVoutResponse {
    pub txid: Txid,
    pub n: usize,
}

#[ocean_endpoint]
async fn list_transactions(
    Path(Address { address }): Path<Address>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<ScriptActivityResponse>> {
    let hid = address_to_hid(&address, ctx.network.into())?;
    let next = query
        .next
        .as_ref()
        .map(|next| {
            let height = &next[0..8];
            let vin_vout_type = &next[8..8+2];
            let txid = &next[8+2..64+8+2];
            let n = &next[64+8+2..];

            let height = height.parse::<u32>()?;
            let vin_vout_type = match vin_vout_type {
               "00" => ScriptActivityTypeHex::Vin,
               _ => ScriptActivityTypeHex::Vout,
            };
            let txid = Txid::from_str(txid)?;
            let n = n.parse::<usize>()?;
            Ok::<(u32, ScriptActivityTypeHex, Txid, usize), Error>((height, vin_vout_type, txid, n))
        })
        .transpose()?
        .unwrap_or((u32::MAX, ScriptActivityTypeHex::Vout, Txid::from_byte_array([0xffu8; 32]), usize::MAX));

    let res = ctx
        .services
        .script_activity
        .by_id
        .list(Some((hid.clone(), next.0, next.1, next.2, next.3)), SortOrder::Descending)?
        .skip(query.next.is_some() as usize)
        .take(query.size)
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == hid,
            _ => true,
        })
        .map(|item| {
            let (_, v) = item?;
            Ok(v.into())
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(res, query.size, |item| {
        item.id.clone()
    }))
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ScriptUnspentResponse {
    pub id: String,
    pub hid: String,
    pub sort: String,
    pub block: BlockContext,
    pub script: ScriptUnspentScriptResponse,
    pub vout: ScriptUnspentVoutResponse,
}

impl From<ScriptUnspent> for ScriptUnspentResponse {
    fn from(v: ScriptUnspent) -> Self {
        Self {
            id: v.id,
            hid: v.hid,
            sort: v.sort,
            block: v.block,
            script: ScriptUnspentScriptResponse {
                r#type: v.script.r#type,
                hex: v.script.hex.to_lower_hex_string(),
            },
            vout: ScriptUnspentVoutResponse {
                txid: v.vout.txid,
                n: v.vout.n,
                value: format!("{:.8}", v.vout.value),
                token_id: v.vout.token_id,
            },
        }
    }
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ScriptUnspentScriptResponse {
    pub r#type: String,
    pub hex: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ScriptUnspentVoutResponse {
    pub txid: Txid,
    pub n: usize,
    pub value: String,
    pub token_id: Option<u32>,
}

#[ocean_endpoint]
async fn list_transaction_unspent(
    Path(Address { address }): Path<Address>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<ScriptUnspentResponse>> {
    let hid = address_to_hid(&address, ctx.network.into())?;

    let next = query
        .next
        .as_ref()
        .map(|next| {
            let height = &next[0..8];
            let txid = &next[8..64 + 8];
            let n = &next[64 + 8..];

            let txid = Txid::from_str(txid)?;
            Ok::<(String, Txid, String), Error>((height.to_string(), txid, n.to_string()))
        })
        .transpose()?
        .unwrap_or((
            "0".to_string(),
            Txid::from_byte_array([0x00u8; 32]),
            "0".to_string(),
        ));

    let res = ctx
        .services
        .script_unspent
        .by_id
        .list(
            Some((hid.clone(), next.0, next.1, next.2)),
            SortOrder::Ascending,
        )?
        .skip(query.next.is_some() as usize)
        .take(query.size)
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == hid.clone(),
            _ => true,
        })
        .map(|item| {
            let (_, v) = item?;
            let res = v.into();
            Ok(res)
        })
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
        .route("/:address/transactions", get(list_transactions))
        .route(
            "/:address/transactions/unspent",
            get(list_transaction_unspent),
        )
        .layer(Extension(ctx))
}
