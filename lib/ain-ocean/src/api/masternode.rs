use axum::{extract::{Path, Query}, routing::get, Router, Json};

use crate::{
    api_paged_response::ApiPagedResponse,
    api_query::PaginationQuery,
    error::OceanResult,
    model::masternode::{MasternodeData, MasternodeState, MasternodeOwner, MasternodeOperator},
};

async fn list_masternodes(Query(query): Query<PaginationQuery>) -> OceanResult<Json<ApiPagedResponse<Masternode>>> {
    let masternodes = vec![
        MasternodeData {
            id: "e86c027861cc0af423313f4152a44a83296a388eb51bf1a6dde9bd75bed55fb4".into(),
            sort: "00000000e86c027861cc0af423313f4152a44a83296a388eb51bf1a6dde9bd75bed55fb4".into(),
            state: MasternodeState::Enabled,
            minted_blocks: 2,
            owner: MasternodeOwner {
                address: "mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU".into(),
            },
            operator: MasternodeOperator {
                address: "mswsMVsyGMj1FzDMbbxw2QW3KvQAv2FKiy".into(),
            },
            creation: MasternodeCreation {
                height: 0
            },
            resign: None,
            timelock: 0
        }
    ];
    Ok(Json(ApiPagedResponse::of(masternodes, query.size, |masternode| {
        masternode.clone().id
    })))
}

async fn get_masternode(Path(masternode_id): Path<String>) -> OceanResult<Json<Masternode>> {
    Ok(Json(Masternode {
        id: "e86c027861cc0af423313f4152a44a83296a388eb51bf1a6dde9bd75bed55fb4".into(),
        sort: "00000000e86c027861cc0af423313f4152a44a83296a388eb51bf1a6dde9bd75bed55fb4".into(),
        state: MasternodeState::Enabled,
        minted_blocks: 2,
        owner: MasternodeOwner {
            address: "mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU".into(),
        },
        operator: MasternodeOperator {
            address: "mswsMVsyGMj1FzDMbbxw2QW3KvQAv2FKiy".into(),
        },
        creation: MasternodeCreation {
            height: 0
        },
        resign: None,
        timelock: 0
    }))
}

pub fn router() -> Router {
    Router::new()
        .route("/", get(list_masternodes))
        .route("/:id", get(get_masternode))
}
