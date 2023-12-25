use axum::{
    extract::{Path, Query},
    routing::get,
    Json, Router,
};
use serde::{Deserialize, Serialize};

use crate::{
    api_paged_response::ApiPagedResponse, api_query::PaginationQuery, error::OceanResult,
    model::Masternode, repository::RepositoryOps, SERVICES,
};

#[derive(Serialize, Deserialize, Debug, Default, Clone)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum MasternodeState {
    PreEnabled,
    Enabled,
    PreResigned,
    Resigned,
    PreBanned,
    Banned,
    #[default]
    Unknown,
}

#[derive(Serialize, Deserialize, Debug, Default, Clone)]
pub struct MasternodeOwner {
    pub address: String,
}

#[derive(Serialize, Deserialize, Debug, Default, Clone)]
pub struct MasternodeOperator {
    pub address: String,
}

#[derive(Serialize, Deserialize, Debug, Default, Clone)]
pub struct MasternodeCreation {
    pub height: u32,
}

#[derive(Serialize, Deserialize, Debug, Default, Clone)]
pub struct MasternodeResign {
    pub tx: String,
    pub height: i32,
}

#[derive(Serialize, Deserialize, Debug, Default, Clone)]
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

impl From<Masternode> for MasternodeData {
    fn from(v: Masternode) -> Self {
        MasternodeData {
            id: v.id,
            sort: v.sort,
            state: MasternodeState::default(), // TODO Handle mn state
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
                height: v.resign_height,
            }),
            timelock: v.timelock,
        }
    }
}

async fn list_masternodes(
    Query(query): Query<PaginationQuery>,
) -> OceanResult<Json<ApiPagedResponse<MasternodeData>>> {
    let next = query
        .next
        .map(|q| {
            let parts = q.split('-').collect::<Vec<_>>();
            if parts.len() != 2 {
                return Err("Invalid query format");
            }

            let height = parts[0].parse::<u32>().map_err(|_| "Invalid height")?;
            let txno = parts[1].parse::<usize>().map_err(|_| "Invalid txno")?;

            Ok((height, txno))
        })
        .transpose()?;

    let masternodes = SERVICES
        .masternode
        .by_height
        .list(next, query.size)?
        .iter()
        .map(|(_, mn_id)| {
            let id: bitcoin::Txid = mn_id.parse()?;
            let mn = SERVICES
                .masternode
                .by_id
                .get(id)?
                .ok_or("Missing masternode index")?;

            Ok(mn.into())
        })
        .collect::<OceanResult<Vec<_>>>()?;

    Ok(Json(ApiPagedResponse::of(
        masternodes,
        query.size,
        |masternode| masternode.clone().id,
    )))
}

async fn get_masternode(
    Path(masternode_id): Path<String>,
) -> OceanResult<Json<Option<MasternodeData>>> {
    let id: bitcoin::Txid = masternode_id.parse()?;
    let mn = SERVICES.masternode.by_id.get(id)?.map(Into::into);

    Ok(Json(mn))
}

pub fn router() -> Router {
    Router::new()
        .route("/", get(list_masternodes))
        .route("/:id", get(get_masternode))
}
