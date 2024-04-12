use std::sync::Arc;

mod state;

use ain_macros::ocean_endpoint;
use anyhow::format_err;
use axum::{
    extract::{Path, Query},
    routing::get,
    Extension, Router,
};
use bitcoin::Txid;
use serde::{Deserialize, Serialize};

use self::state::{MasternodeService, MasternodeState};

use super::{
    query::PaginationQuery,
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    api::common::Paginate,
    error::{ApiError, Error, NotFoundKind},
    model::Masternode,
    repository::{RepositoryOps, SecondaryIndex},
    storage::SortOrder,
    Result,
};

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct MasternodeOwner {
    pub address: String,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct MasternodeOperator {
    pub address: String,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct MasternodeCreation {
    pub height: u32,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct MasternodeResign {
    pub tx: Txid,
    pub height: i64,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct MasternodeData {
    pub id: String,
    pub sort: String,
    pub state: MasternodeState,
    pub minted_blocks: i32,
    pub owner: MasternodeOwner,
    pub operator: MasternodeOperator,
    pub creation: MasternodeCreation,
    pub resign: Option<MasternodeResign>,
    pub timelock: u16,
}

impl MasternodeData {
    fn from_with_state(v: Masternode, state: MasternodeState) -> Self {
        MasternodeData {
            id: v.id.to_string(),
            sort: format!("{:08x}{}", v.block.height, v.id),
            state,
            minted_blocks: v.minted_blocks,
            owner: MasternodeOwner {
                address: v.owner_address,
            },
            operator: MasternodeOperator {
                address: v.operator_address,
            },
            creation: MasternodeCreation {
                height: v.creation_height,
            },
            resign: v.resign_tx.map(|tx| MasternodeResign {
                tx,
                height: match v.resign_height {
                    None => -1,
                    Some(v) => v as i64,
                },
            }),
            timelock: v.timelock,
        }
    }
}

#[ocean_endpoint]
async fn list_masternodes(
    Query(query): Query<PaginationQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<MasternodeData>> {
    let repository = &ctx.services.masternode.by_height;
    let next = query
        .next
        .as_ref()
        .map(|q| {
            let height = q[0..8]
                .parse::<u32>()
                .map_err(|_| format_err!("Invalid height"))?;
            let txid = q[8..]
                .parse::<Txid>()
                .map_err(|_| format_err!("Invalid txid"))?;

            Ok::<(u32, bitcoin::Txid), Error>((height, txid))
        })
        .transpose()?;

    let height = ctx
        .services
        .block
        .by_height
        .get_highest()?
        .map_or(0, |b| b.height);

    let masternodes = repository
        .list(next, SortOrder::Descending)?
        .paginate(&query)
        .map(|el| repository.retrieve_primary_value(el))
        .map(|v| {
            let mn = v.unwrap();
            let state = MasternodeService::new(ctx.network).get_masternode_state(&mn, height);
            Ok(MasternodeData::from_with_state(mn, state))
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(ApiPagedResponse::of(
        masternodes,
        query.size,
        |masternode| masternode.sort.to_string(),
    ))
}

#[ocean_endpoint]
async fn get_masternode(
    Path(masternode_id): Path<Txid>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<MasternodeData>> {
    let height = ctx
        .services
        .block
        .by_height
        .get_highest()?
        .map_or(0, |b| b.height);

    let mn = ctx
        .services
        .masternode
        .by_id
        .get(&masternode_id)?
        .map(|mn| {
            let state = MasternodeService::new(ctx.network).get_masternode_state(&mn, height);
            MasternodeData::from_with_state(mn, state)
        })
        .ok_or(Error::NotFound(NotFoundKind::Masternode))?;

    Ok(Response::new(mn))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/", get(list_masternodes))
        .route("/:id", get(get_masternode))
        .layer(Extension(ctx))
}
