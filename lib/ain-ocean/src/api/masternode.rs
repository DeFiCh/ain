use axum::{
    extract::{Path, Query},
    routing::get,
    Json, Router,
};

use crate::{
    api_paged_response::ApiPagedResponse,
    api_query::PaginationQuery,
    error::OceanResult,
    model::{MasternodeData, MasternodeState},
    repository::RepositoryOps,
    SERVICES,
};

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

            Ok(MasternodeData {
                id: mn.id,
                sort: mn.sort,
                state: MasternodeState::Enabled, // TODO Handle mn state
                minted_blocks: mn.minted_blocks,
                owner: mn.owner_address,
                operator: mn.operator_address,
                creation: mn.creation_height,
                resign: None,
                timelock: mn.timelock,
            })
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
    let mn = SERVICES.masternode.by_id.get(id)?.map(|mn| MasternodeData {
        id: mn.id,
        sort: mn.sort,
        state: MasternodeState::Enabled, // TODO Handle mn state
        minted_blocks: mn.minted_blocks,
        owner: mn.owner_address,
        operator: mn.operator_address,
        creation: mn.creation_height,
        resign: None,
        timelock: mn.timelock,
    });

    Ok(Json(mn))
}

pub fn router() -> Router {
    Router::new()
        .route("/", get(list_masternodes))
        .route("/:id", get(get_masternode))
}
