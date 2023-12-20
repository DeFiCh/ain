use axum::{
    extract::{Path, Query},
    routing::get,
    Json, Router,
};

use crate::{
    api_paged_response::ApiPagedResponse, api_query::PaginationQuery, error::OceanResult,
    model::masternode::MasternodeData,
};

async fn list_masternodes(
    Query(query): Query<PaginationQuery>,
) -> OceanResult<Json<ApiPagedResponse<MasternodeData>>> {
    let masternodes = vec![MasternodeData::new("0")];
    Ok(Json(ApiPagedResponse::of(
        masternodes,
        query.size,
        |masternode| masternode.clone().id,
    )))
}

async fn get_masternode(Path(masternode_id): Path<String>) -> OceanResult<Json<MasternodeData>> {
    Ok(Json(MasternodeData::new(&masternode_id)))
}

pub fn router() -> Router {
    Router::new()
        .route("/", get(list_masternodes))
        .route("/:id", get(get_masternode))
}
