use std::{str::FromStr, sync::Arc};

use ain_dftx::COIN;
use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use bitcoin::{hashes::Hash, ScriptBuf, Txid};
use defichain_rpc::{
    defichain_rpc_json::{
        loan::{CollateralTokenDetail, LoanSchemeResult},
        token::TokenInfo,
        vault::{VaultActive, VaultLiquidationBatch},
    },
    json::vault::{
        AuctionPagination, AuctionPaginationStart, ListVaultOptions, VaultLiquidation,
        VaultPagination, VaultResult, VaultState,
    },
    LoanRPC, VaultRPC,
};
use futures::future::try_join_all;
use log::trace;
use rust_decimal::Decimal;
use serde::{Deserialize, Serialize, Serializer};
use serde_with::skip_serializing_none;
use snafu::OptionExt;

use super::{
    cache::{get_loan_scheme_cached, get_token_cached},
    common::{
        from_script, parse_amount, parse_display_symbol, parse_fixed_interval_price,
        parse_query_height_txid, Paginate,
    },
    path::Path,
    query::{PaginationQuery, Query},
    response::{ApiPagedResponse, Response},
    tokens::TokenData,
    AppContext,
};
use crate::{
    error::{ApiError, Error, NotFoundKind, NotFoundSnafu},
    model::{BlockContext, OraclePriceActive, VaultAuctionBatchHistory},
    network::Network,
    storage::{RepositoryOps, SortOrder},
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
        loan_scheme.id.clone()
    }))
}

#[ocean_endpoint]
async fn get_scheme(
    Path(scheme_id): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<LoanSchemeData>> {
    let scheme = ctx
        .client
        .get_loan_scheme(scheme_id)
        .await
        .map_err(|_| Error::NotFound {
            kind: NotFoundKind::Scheme,
        })?;
    Ok(Response::new(scheme.into()))
}

#[skip_serializing_none]
#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CollateralToken {
    token_id: String,
    token: TokenData,
    factor: String,
    activate_after_block: u32,
    fixed_interval_price_id: String,
    active_price: Option<OraclePriceActive>,
}

impl CollateralToken {
    fn from_with_id(
        id: String,
        detail: CollateralTokenDetail,
        info: TokenInfo,
        active_price: Option<OraclePriceActive>,
    ) -> Self {
        Self {
            token_id: detail.token_id,
            factor: format!("{}", detail.factor),
            activate_after_block: 0,
            fixed_interval_price_id: detail.fixed_interval_price_id,
            token: TokenData::from_with_id(id, info),
            active_price,
        }
    }
}

fn get_active_price(
    ctx: &Arc<AppContext>,
    fixed_interval_price_id: String,
) -> Result<Option<OraclePriceActive>> {
    let (token, currency) = parse_fixed_interval_price(&fixed_interval_price_id)?;
    let price = ctx
        .services
        .oracle_price_active
        .by_id
        .list(Some((token.clone(), currency.clone(), [0xffu8; 4])), SortOrder::Descending)?
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == token && k.1 == currency,
            _ => true,
        })
        .next()
        .map(|item| {
            let (_, v) = item?;
            Ok::<OraclePriceActive, Error>(v)
        })
        .transpose()?;

    Ok(price)
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
            let (id, info) = get_token_cached(&ctx, &v.token_id)
                .await?
                .context(NotFoundSnafu {
                    kind: NotFoundKind::Token {
                        id: v.token_id.clone(),
                    },
                })?;
            let active_price = get_active_price(&ctx, v.fixed_interval_price_id.clone())?;
            Ok::<CollateralToken, Error>(CollateralToken::from_with_id(id, v, info, active_price))
        })
        .collect::<Vec<_>>();

    let res = try_join_all(fut).await?;

    Ok(ApiPagedResponse::of(res, query.size, |loan_scheme| {
        loan_scheme.token_id.clone()
    }))
}

#[ocean_endpoint]
async fn get_collateral_token(
    Path(token_id): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<CollateralToken>> {
    let collateral_token = ctx
        .client
        .get_collateral_token(token_id)
        .await
        .map_err(|_| Error::NotFound {
            kind: NotFoundKind::CollateralToken,
        })?;
    let (id, info) = get_token_cached(&ctx, &collateral_token.token_id)
        .await?
        .context(NotFoundSnafu {
            kind: NotFoundKind::Token {
                id: collateral_token.token_id.clone(),
            },
        })?;
    let active_price = get_active_price(&ctx, collateral_token.fixed_interval_price_id.clone())?;

    Ok(Response::new(CollateralToken::from_with_id(
        id,
        collateral_token,
        info,
        active_price,
    )))
}

#[skip_serializing_none]
#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct LoanToken {
    token_id: String,
    token: TokenData,
    interest: String,
    fixed_interval_price_id: String,
    active_price: Option<OraclePriceActive>,
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
        fixed_interval_price_id: String,
        interest: f64,
    }

    let res = tokens
        .into_iter()
        .filter_map(|el| {
            el.token
                .0
                .into_iter()
                .next() // Should always get a Hashmap<id, data> with single entry here.
                .map(|(id, data)| FlattenToken {
                    id,
                    data,
                    fixed_interval_price_id: el.fixed_interval_price_id,
                    interest: el.interest,
                })
        })
        .fake_paginate(&query, |token| match &query.next {
            None => false,
            Some(v) => v != &token.data.creation_tx,
        })
        .map(|flatten_token| {
            let fixed_interval_price_id = flatten_token.fixed_interval_price_id.clone();
            let (token, currency) = parse_fixed_interval_price(&fixed_interval_price_id)?;

            let active_price = ctx
                .services
                .oracle_price_active
                .by_id
                .list(Some((token, currency, [0xffu8; 4])), SortOrder::Descending)?
                .next()
                .map(|item| {
                    let (_, v) = item?;
                    Ok::<OraclePriceActive, Error>(v)
                })
                .transpose()?;

            let token = LoanToken {
                token_id: flatten_token.data.creation_tx.clone(),
                token: TokenData::from_with_id(flatten_token.id, flatten_token.data),
                interest: format!("{:.2}", flatten_token.interest),
                fixed_interval_price_id,
                active_price,
            };
            Ok::<LoanToken, Error>(token)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(res, query.size, |loan_scheme| {
        loan_scheme.token_id.clone()
    }))
}

#[ocean_endpoint]
async fn get_loan_token(
    Path(token_id): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<LoanToken>> {
    let loan_token_result = ctx
        .client
        .get_loan_token(token_id.clone())
        .await
        .map_err(|_| Error::NotFound {
            kind: NotFoundKind::LoanToken,
        })?;
    let Some(token) = loan_token_result
        .token
        .0
        .into_iter()
        .next()
        .map(|(id, info)| {
            let fixed_interval_price_id = loan_token_result.fixed_interval_price_id.clone();
            let active_price = get_active_price(&ctx, fixed_interval_price_id.clone())?;

            Ok::<LoanToken, Error>(LoanToken {
                token_id: info.creation_tx.clone(),
                token: TokenData::from_with_id(id, info),
                interest: format!("{:.2}", loan_token_result.interest),
                fixed_interval_price_id,
                active_price,
            })
        })
        .transpose()?
    else {
        return Err(Error::NotFound {
            kind: NotFoundKind::LoanToken,
        });
    };

    Ok(Response::new(token))
}

pub async fn get_all_vaults(
    ctx: &Arc<AppContext>,
    options: ListVaultOptions,
    query: &PaginationQuery,
) -> Result<Vec<VaultResponse>> {
    let pagination = VaultPagination {
        start: query.next.clone(),
        including_start: None,
        limit: if query.size > 30 {
            Some(30)
        } else {
            Some(query.size)
        },
    };

    let vaults = ctx.client.list_vaults(options, pagination).await?;
    let mut list = Vec::new();
    for vault in vaults {
        let each = match vault {
            VaultResult::VaultActive(vault) => {
                VaultResponse::Active(map_vault_active(ctx, vault).await?)
            }
            VaultResult::VaultLiquidation(vault) => {
                VaultResponse::Liquidated(map_vault_liquidation(ctx, vault).await?)
            }
        };
        list.push(each);
    }

    Ok(list)
}

#[ocean_endpoint]
async fn list_vaults(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<VaultResponse>> {
    let options = ListVaultOptions {
        verbose: Some(true),
        owner_address: None,
        loan_scheme_id: None,
        state: None,
    };
    let list = get_all_vaults(&ctx, options, &query).await?;

    Ok(ApiPagedResponse::of(list, query.size, |each| match each {
        VaultResponse::Active(vault) => vault.vault_id.clone(),
        VaultResponse::Liquidated(vault) => vault.vault_id.clone(),
    }))
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct LoanScheme {
    id: String,
    min_col_ratio: String,
    interest_rate: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct VaultActiveResponse {
    pub vault_id: String,
    loan_scheme: LoanScheme,
    owner_address: String,
    #[serde(default = "VaultState::Active")]
    state: VaultState,
    informative_ratio: String,
    collateral_ratio: String,
    collateral_value: String,
    loan_value: String,
    interest_value: String,
    collateral_amounts: Vec<VaultTokenAmountResponse>,
    loan_amounts: Vec<VaultTokenAmountResponse>,
    interest_amounts: Vec<VaultTokenAmountResponse>,
}

async fn map_loan_scheme(ctx: &Arc<AppContext>, id: String) -> Result<LoanScheme> {
    let loan_scheme = get_loan_scheme_cached(ctx, id).await?;
    Ok(LoanScheme {
        id: loan_scheme.id,
        min_col_ratio: loan_scheme.mincolratio.to_string(),
        interest_rate: loan_scheme.interestrate.to_string(),
    })
}

async fn map_vault_active(
    ctx: &Arc<AppContext>,
    vault: VaultActive,
) -> Result<VaultActiveResponse> {
    Ok(VaultActiveResponse {
        vault_id: vault.vault_id,
        loan_scheme: map_loan_scheme(ctx, vault.loan_scheme_id).await?,
        owner_address: vault.owner_address,
        state: VaultState::Active,
        informative_ratio: vault.informative_ratio.to_string(),
        collateral_ratio: vault.collateral_ratio.to_string(),
        collateral_value: vault.collateral_value.to_string(),
        loan_value: vault.loan_value.to_string(),
        interest_value: vault.interest_value.to_string(),
        collateral_amounts: map_token_amounts(ctx, vault.collateral_amounts).await?,
        loan_amounts: map_token_amounts(ctx, vault.loan_amounts).await?,
        interest_amounts: map_token_amounts(ctx, vault.interest_amounts).await?,
    })
}

async fn map_vault_liquidation(
    ctx: &Arc<AppContext>,
    vault: VaultLiquidation,
) -> Result<VaultLiquidatedResponse> {
    let loan_scheme = get_loan_scheme_cached(ctx, vault.loan_scheme_id).await?;
    Ok(VaultLiquidatedResponse {
        batches: map_liquidation_batches(ctx, &vault.vault_id, vault.batches).await?,
        vault_id: vault.vault_id,
        loan_scheme,
        owner_address: vault.owner_address,
        state: vault.state,
        liquidation_height: vault.liquidation_height,
        liquidation_penalty: vault.liquidation_penalty,
        batch_count: vault.batch_count,
    })
}

pub enum VaultResponse {
    Active(VaultActiveResponse),
    Liquidated(VaultLiquidatedResponse),
}

impl Serialize for VaultResponse {
    fn serialize<S>(&self, serializer: S) -> std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match self {
            Self::Active(v) => v.serialize(serializer),
            Self::Liquidated(v) => v.serialize(serializer),
        }
    }
}

#[ocean_endpoint]
async fn get_vault(
    Path(vault_id): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<VaultResponse>> {
    let vault = ctx
        .client
        .get_vault(vault_id, Some(false))
        .await
        .map_err(|_| Error::NotFound {
            kind: NotFoundKind::Vault,
        })?;
    let res = match vault {
        VaultResult::VaultActive(vault) => {
            VaultResponse::Active(map_vault_active(&ctx, vault).await?)
        }
        VaultResult::VaultLiquidation(vault) => {
            VaultResponse::Liquidated(map_vault_liquidation(&ctx, vault).await?)
        }
    };

    Ok(Response::new(res))
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct VaultAuctionBatchHistoryResponse {
    pub id: String,
    pub key: String,
    pub sort: String,
    pub vault_id: Txid,
    pub index: u32,
    pub from: ScriptBuf,
    pub address: String,
    pub amount: String,
    pub token_id: u64,
    pub block: BlockContext,
}

impl VaultAuctionBatchHistoryResponse {
    fn from(
        data: ((Txid, [u8; 4], [u8; 4], Txid), VaultAuctionBatchHistory),
        address: String,
    ) -> Self {
        let id = data.0;
        let vault_id = id.0;
        let batch_index = u32::from_be_bytes(id.1);
        let block_height = id.2;
        let txid = id.3;
        let history = data.1;
        let amount = Decimal::from(history.amount) / Decimal::from(COIN);

        Self {
            id: format!(
                "{}-{}-{}",
                vault_id.clone(),
                batch_index.clone(),
                txid.clone()
            ),
            key: format!("{}-{}", vault_id.clone(), batch_index.clone()),
            sort: format!("{}-{}", hex::encode(block_height), txid),
            vault_id,
            index: batch_index,
            from: history.from,
            address,
            amount: amount.to_string(),
            token_id: history.token_id,
            block: history.block,
        }
    }
}

#[ocean_endpoint]
async fn list_vault_auction_history(
    Path((vault_id, liquidation_height, batch_index)): Path<(Txid, u32, u32)>,
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<VaultAuctionBatchHistoryResponse>> {
    trace!(
        "Auction history for vault id {}, liquidation_height {}, batch index {}",
        vault_id,
        liquidation_height,
        batch_index
    );
    let (liquidation_height, txid) = query
        .next
        .clone()
        .map(|q| {
            let (height, txid) = parse_query_height_txid(&q)?;
            Ok::<(u32, Txid), Error>((height, txid))
        })
        .transpose()?
        .unwrap_or((liquidation_height, Txid::from_byte_array([0xffu8; 32])));

    let liquidation_block_expiry = match ctx.network {
        Network::Regtest => 36,
        _ => 720,
    };

    let auctions = ctx
        .services
        .auction
        .by_id
        .list(
            Some((
                vault_id,
                batch_index.to_be_bytes(),
                liquidation_height.to_be_bytes(),
                txid,
            )),
            SortOrder::Descending,
        )?
        .take_while(|item| match item {
            Ok((k, _)) => {
                k.0 == vault_id
                    && k.1 == batch_index.to_be_bytes()
                    && u32::from_be_bytes(k.2) > liquidation_height - liquidation_block_expiry
            }
            _ => true,
        })
        .take(query.size + usize::from(query.next.is_some()))
        .skip(usize::from(query.next.is_some()))
        .map(|item| {
            let (id, history) = item?;
            let address = from_script(&history.from, ctx.network)?;
            Ok(VaultAuctionBatchHistoryResponse::from(
                (id, history),
                address,
            ))
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(auctions, query.size, |auction| {
        auction.sort.to_string()
    }))
}

#[derive(Serialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct VaultLiquidatedResponse {
    pub vault_id: String,
    pub loan_scheme: LoanSchemeResult,
    pub owner_address: String,
    #[serde(default = "VaultState::in_liquidation")]
    pub state: VaultState,
    pub liquidation_height: u64,
    pub liquidation_penalty: f64,
    pub batch_count: usize,
    pub batches: Vec<VaultLiquidatedBatchResponse>,
}

#[skip_serializing_none]
#[derive(Serialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct HighestBidResponse {
    pub owner: String,
    pub amount: Option<VaultTokenAmountResponse>,
}

#[skip_serializing_none]
#[derive(Serialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct VaultLiquidatedBatchResponse {
    index: u32,
    collaterals: Vec<VaultTokenAmountResponse>,
    loan: Option<VaultTokenAmountResponse>,
    highest_bid: Option<HighestBidResponse>,
    froms: Vec<String>,
}

#[skip_serializing_none]
#[derive(Serialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct VaultTokenAmountResponse {
    pub id: String,
    pub amount: String,
    pub symbol: String,
    pub display_symbol: String,
    pub symbol_key: String,
    pub name: String,
    pub active_price: Option<OraclePriceActive>,
}

async fn map_liquidation_batches(
    ctx: &Arc<AppContext>,
    vault_id: &str,
    batches: Vec<VaultLiquidationBatch>,
) -> Result<Vec<VaultLiquidatedBatchResponse>> {
    let repo = &ctx.services.auction;
    let mut vec = Vec::new();
    for batch in batches {
        let highest_bid = if let Some(bid) = batch.highest_bid {
            let amount = map_token_amounts(ctx, vec![bid.amount]).await?;
            let res = HighestBidResponse {
                owner: bid.owner,
                amount: amount.first().cloned(),
            };
            Some(res)
        } else {
            None
        };
        let id = (
            Txid::from_str(vault_id)?,
            batch.index.to_be_bytes(),
            [0xffu8; 4],
            Txid::from_byte_array([0xffu8; 32]),
        );
        let bids = repo
            .by_id
            .list(Some(id), SortOrder::Descending)?
            .take_while(|item| match item {
                Ok(((vid, bindex, _, _), _)) => {
                    vid.to_string() == vault_id && bindex == &batch.index.to_be_bytes()
                }
                _ => true,
            })
            .collect::<Vec<_>>();
        let froms = bids
            .into_iter()
            .map(|bid| {
                let (_, v) = bid?;
                let from_addr = from_script(&v.from, ctx.network)?;
                Ok::<String, Error>(from_addr)
            })
            .collect::<Result<Vec<_>>>()?;
        vec.push(VaultLiquidatedBatchResponse {
            index: batch.index,
            collaterals: map_token_amounts(ctx, batch.collaterals).await?,
            loan: map_token_amounts(ctx, vec![batch.loan])
                .await?
                .first()
                .cloned(),
            froms,
            highest_bid,
        });
    }
    Ok(vec)
}

async fn map_token_amounts(
    ctx: &Arc<AppContext>,
    amounts: Vec<String>,
) -> Result<Vec<VaultTokenAmountResponse>> {
    if amounts.is_empty() {
        return Ok(Vec::new());
    }
    let amount_token_symbols = amounts
        .into_iter()
        .map(|amount| {
            let (amount, token_symbol) = parse_amount(&amount)?;
            Ok::<[String; 2], Error>([amount, token_symbol])
        })
        .collect::<Result<Vec<_>>>()?;

    let mut vault_token_amounts = Vec::new();
    for [amount, token_symbol] in amount_token_symbols {
        let Some((id, token_info)) = get_token_cached(ctx, &token_symbol).await? else {
            log::error!("Token {token_symbol} not found");
            continue;
        };

        let active_price = ctx
            .services
            .oracle_price_active
            .by_id
            .list(
                Some((token_info.symbol.clone(), "USD".to_string(), [0xffu8; 4])),
                SortOrder::Descending,
            )?
            .take_while(|item| match item {
                Ok((k, _)) => k.0 == token_info.symbol && k.1 == "USD",
                _ => true,
            })
            .next()
            .map(|item| {
                let (_, v) = item?;
                Ok::<OraclePriceActive, Error>(v)
            })
            .transpose()?;

        vault_token_amounts.push(VaultTokenAmountResponse {
            id,
            display_symbol: parse_display_symbol(&token_info),
            amount: amount.to_string(),
            symbol: token_info.symbol,
            symbol_key: token_info.symbol_key,
            name: token_info.name,
            active_price,
        });
    }

    Ok(vault_token_amounts)
}

#[ocean_endpoint]
async fn list_auctions(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<VaultLiquidatedResponse>> {
    let start = query.next.as_ref().map(|next| {
        let vault_id = &next[0..64];
        let height = &next[64..];
        AuctionPaginationStart {
            vault_id: vault_id.to_string(),
            height: height.parse::<u64>().unwrap_or_default(),
        }
    });

    let pagination = AuctionPagination {
        start,
        including_start: None,
        limit: if query.size > 30 {
            Some(30)
        } else {
            Some(query.size)
        },
    };

    let mut vaults = Vec::new();
    let liquidation_vaults = ctx.client.list_auctions(Some(pagination)).await?;
    for vault in liquidation_vaults {
        let loan_scheme = get_loan_scheme_cached(&ctx, vault.loan_scheme_id).await?;
        let res = VaultLiquidatedResponse {
            batches: map_liquidation_batches(&ctx, &vault.vault_id, vault.batches).await?,
            vault_id: vault.vault_id,
            loan_scheme,
            owner_address: vault.owner_address,
            state: vault.state,
            liquidation_height: vault.liquidation_height,
            liquidation_penalty: vault.liquidation_penalty,
            batch_count: vault.batch_count,
        };
        vaults.push(res);
    }

    Ok(ApiPagedResponse::of(vaults, query.size, |auction| {
        format!("{}{}", auction.vault_id.clone(), auction.liquidation_height)
    }))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/schemes", get(list_scheme))
        .route("/schemes/:id", get(get_scheme))
        .route("/collaterals", get(list_collateral_token))
        .route("/collaterals/:id", get(get_collateral_token))
        .route("/tokens", get(list_loan_token))
        .route("/tokens/:id", get(get_loan_token))
        .route("/vaults", get(list_vaults))
        .route("/vaults/:id", get(get_vault))
        .route(
            "/vaults/:id/auctions/:height/batches/:batchIndex/history",
            get(list_vault_auction_history),
        )
        .route("/auctions", get(list_auctions))
        .layer(Extension(ctx))
}
