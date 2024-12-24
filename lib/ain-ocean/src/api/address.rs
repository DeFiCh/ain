use std::{collections::BTreeMap, str::FromStr, sync::Arc};

use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use bitcoin::{hashes::Hash, hex::DisplayHex, BlockHash, Txid};
use defichain_rpc::{
    json::{account::AccountHistory, vault::ListVaultOptions},
    AccountRPC, RpcApi,
};
use serde::{Deserialize, Serialize};
use serde_json::json;
use serde_with::skip_serializing_none;

use super::{
    cache::get_token_cached,
    common::{address_to_hid, parse_display_symbol},
    loan::{get_all_vaults, VaultResponse},
    path::Path,
    query::{PaginationQuery, Query},
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    error::ApiError,
    model::{
        BlockContext, ScriptActivity, ScriptActivityTypeHex, ScriptAggregation, ScriptUnspent,
    },
    storage::{RepositoryOps, SortOrder},
    Error, Result,
};

#[derive(Deserialize)]
struct Address {
    address: String,
}

#[derive(Deserialize)]
struct History {
    address: String,
    height: u32,
    txno: u32,
}

#[skip_serializing_none]
#[derive(Debug, Serialize)]
struct AddressHistory {
    owner: String,
    txid: Option<Txid>,
    txn: Option<u64>,
    r#type: String,
    amounts: Vec<String>,
    block: AddressHistoryBlock,
}

impl From<AccountHistory> for AddressHistory {
    fn from(history: AccountHistory) -> Self {
        Self {
            owner: history.owner,
            txid: history.txid,
            txn: history.txn,
            r#type: history.r#type,
            amounts: history.amounts,
            block: AddressHistoryBlock {
                height: history.block_height,
                hash: history.block_hash,
                time: history.block_time,
            },
        }
    }
}

#[skip_serializing_none]
#[derive(Debug, Serialize)]
struct AddressHistoryBlock {
    height: u64,
    hash: Option<BlockHash>,
    time: Option<u64>,
}

#[ocean_endpoint]
async fn get_account_history(
    Path(History {
        address,
        height,
        txno,
    }): Path<History>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<AddressHistory>> {
    let res = ctx
        .client
        .get_account_history(&address, height, txno)
        .await
        .map_err(|_| Error::Other {
            msg: "Record not found".to_string(),
        })?;

    Ok(Response::new(res.into()))
}

// NOTE(canonbrother): deprecated its never being used
// due to unfriendly complicated pagination handling internally
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
            id: format!(
                "{}{}",
                hex::encode(v.block.height.to_be_bytes()),
                hex::encode(v.hid)
            ),
            hid: hex::encode(v.hid),
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
    hid: [u8; 32],
) -> Result<Option<ScriptAggregationResponse>> {
    let latest = ctx
        .services
        .script_aggregation
        .by_id
        .list(Some((hid, [0xffu8; 4])), SortOrder::Descending)?
        .take_while(|item| match item {
            Ok(((v, _), _)) => v == &hid,
            _ => true,
        })
        .next()
        .transpose()?
        .map(|item| {
            let (_, v) = item;
            v.into()
        });

    Ok(latest)
}

#[ocean_endpoint]
async fn get_balance(
    Path(Address { address }): Path<Address>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<String>> {
    let hid = address_to_hid(&address, ctx.network)?;
    let Some(aggregation) = get_latest_aggregation(&ctx, hid)? else {
        return Ok(Response::new("0.00000000".to_string()));
    };

    Ok(Response::new(aggregation.amount.unspent))
}

#[ocean_endpoint]
async fn get_aggregation(
    Path(Address { address }): Path<Address>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<Option<ScriptAggregationResponse>>> {
    let hid = address_to_hid(&address, ctx.network)?;
    let aggregation = get_latest_aggregation(&ctx, hid)?;
    Ok(Response::new(aggregation))
}

#[ocean_endpoint]
async fn list_vaults(
    Path(Address { address }): Path<Address>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<VaultResponse>> {
    let options = ListVaultOptions {
        verbose: Some(true),
        owner_address: Some(address),
        loan_scheme_id: None,
        state: None,
    };
    let vaults = get_all_vaults(&ctx, options, &query).await?;

    Ok(ApiPagedResponse::of(
        vaults,
        query.size,
        |each| match each {
            VaultResponse::Active(vault) => vault.vault_id.clone(),
            VaultResponse::Liquidated(vault) => vault.vault_id.clone(),
        },
    ))
}

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
        let id = match v.type_hex {
            ScriptActivityTypeHex::Vin => {
                // TODO put vin instead ScriptActivityType
                let vin = v.vin.as_ref().unwrap();
                format!(
                    "{}{}{}{}",
                    hex::encode(v.block.height.to_be_bytes()),
                    ScriptActivityTypeHex::Vin,
                    vin.txid,
                    hex::encode(vin.n.to_be_bytes())
                )
            }
            ScriptActivityTypeHex::Vout => {
                let vout = v.vout.as_ref().unwrap();
                format!(
                    "{}{}{}{}",
                    hex::encode(v.block.height.to_be_bytes()),
                    ScriptActivityTypeHex::Vout,
                    v.txid,
                    hex::encode(vout.n.to_be_bytes())
                )
            }
        };
        Self {
            id,
            hid: hex::encode(v.hid),
            r#type: v.r#type.to_string(),
            type_hex: v.type_hex.to_string(),
            txid: v.txid,
            block: v.block,
            script: ScriptActivityScriptResponse {
                r#type: v.script.r#type,
                hex: v.script.hex.to_lower_hex_string(),
            },
            vin: v.vin.map(|vin| ScriptActivityVinVoutResponse {
                txid: vin.txid,
                n: vin.n,
            }),
            vout: v.vout.map(|vout| ScriptActivityVinVoutResponse {
                txid: vout.txid,
                n: vout.n,
            }),
            value: format!("{:.8}", v.value),
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
    let hid = address_to_hid(&address, ctx.network)?;
    let next = query
        .next
        .as_ref()
        .map(|next| {
            let height = &next[0..8];
            let vin_vout_type = &next[8..8 + 2];
            let txid = &next[8 + 2..64 + 8 + 2];
            let n = &next[64 + 8 + 2..];

            let decoded_height = hex::decode(height)?;
            let height = decoded_height.try_into().map_err(|_| Error::Other {
                msg: format!("Invalid height: {}", height),
            })?;
            let vin_vout_type = match vin_vout_type {
                "00" => ScriptActivityTypeHex::Vin,
                _ => ScriptActivityTypeHex::Vout,
            };
            let txid = Txid::from_str(txid)?;
            let n = n.parse::<usize>()?.to_be_bytes();
            Ok::<([u8; 4], ScriptActivityTypeHex, Txid, [u8; 8]), Error>((
                height,
                vin_vout_type,
                txid,
                n,
            ))
        })
        .transpose()?
        .unwrap_or((
            [0xffu8; 4],
            ScriptActivityTypeHex::Vout,
            Txid::from_byte_array([0xffu8; 32]),
            [0xffu8; 8],
        ));

    let res = ctx
        .services
        .script_activity
        .by_id
        .list(
            Some((hid, next.0, next.1, next.2, next.3)),
            SortOrder::Descending,
        )?
        .skip(usize::from(query.next.is_some()))
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
            id: format!("{}{}", v.id.0, hex::encode(v.id.1)),
            hid: hex::encode(v.hid),
            sort: format!(
                "{}{}{}",
                hex::encode(v.block.height.to_be_bytes()),
                v.vout.txid,
                hex::encode(v.vout.n.to_be_bytes())
            ),
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

#[skip_serializing_none]
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
    let hid = address_to_hid(&address, ctx.network)?;

    let next = query
        .next
        .as_ref()
        .map(|next| {
            let height = &next[0..8];
            let txid = &next[8..64 + 8];
            let n = &next[64 + 8..];

            let decoded_height = hex::decode(height)?;
            let height = decoded_height.try_into().map_err(|_| Error::Other {
                msg: format!("Invalid height: {}", height),
            })?;
            let txid = Txid::from_str(txid)?;
            let decoded_n = hex::decode(n)?;
            let n = decoded_n.try_into().map_err(|_| Error::Other {
                msg: format!("Invalid txno: {}", n),
            })?;
            Ok::<([u8; 4], Txid, [u8; 8]), Error>((height, txid, n))
        })
        .transpose()?
        .unwrap_or(([0u8; 4], Txid::from_byte_array([0x00u8; 32]), [0u8; 8]));

    let res = ctx
        .services
        .script_unspent
        .by_id
        .list(Some((hid, next.0, next.1, next.2)), SortOrder::Ascending)?
        .skip(usize::from(query.next.is_some()))
        .take(query.size)
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == hid,
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

// Tokens owned by an address
#[derive(Serialize, Debug)]
#[serde(rename_all = "camelCase")]
struct AddressToken {
    id: String,
    amount: String,
    symbol: String,
    display_symbol: String,
    symbol_key: String,
    name: String,
    #[serde(rename = "isDAT")]
    is_dat: bool,
    #[serde(rename = "isLPS")]
    is_lps: bool,
    is_loan_token: bool,
}

#[ocean_endpoint]
async fn list_tokens(
    Path(Address { address }): Path<Address>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<AddressToken>> {
    let account: BTreeMap<String, f64> = ctx.client.call(
        "getaccount",
        &[
            address.into(),
            json!({
                "limit": query.size,
                "start": query.next.as_ref().and_then(|n| n.parse::<u32>().ok()).unwrap_or_default(),
                "including_start": query.next.is_none()
            }),
            true.into(),
        ],
    ).await?;

    let mut vec = Vec::new();
    for (k, v) in account {
        let Some((id, info)) = get_token_cached(&ctx, &k).await? else {
            continue;
        };

        let address_token = AddressToken {
            id,
            amount: format!("{v:.8}"),
            display_symbol: parse_display_symbol(&info),
            symbol: info.symbol,
            symbol_key: info.symbol_key,
            name: info.name,
            is_dat: info.is_dat,
            is_lps: info.is_lps,
            is_loan_token: info.is_loan_token,
        };
        vec.push(address_token);
    }

    Ok(ApiPagedResponse::of(vec, query.size, |item| {
        item.id.clone()
    }))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/:address/history/:height/:txno", get(get_account_history))
        // .route("/history", get(list_account_history))
        .route("/:address/balance", get(get_balance))
        .route("/:address/aggregation", get(get_aggregation))
        .route("/:address/tokens", get(list_tokens))
        .route("/:address/vaults", get(list_vaults))
        .route("/:address/transactions", get(list_transactions))
        .route(
            "/:address/transactions/unspent",
            get(list_transaction_unspent),
        )
        .layer(Extension(ctx))
}
