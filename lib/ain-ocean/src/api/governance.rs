use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{extract::Path, routing::get, Extension, Router};
use bitcoin::Txid;
use defichain_rpc::{json::governance::*, GovernanceRPC};
use serde::Deserialize;

use super::response::{ApiPagedResponse, Response};
use crate::{
    api_query::{PaginationQuery, Query},
    error::{ApiError, NotFoundKind, OceanError},
    Result, Services,
};

#[derive(Deserialize, Default)]
pub struct GovernanceQuery {
    #[serde(flatten)]
    pub pagination: PaginationQuery,
    pub status: Option<ListProposalsStatus>,
    pub r#type: Option<ListProposalsType>,
    pub cycle: Option<u64>,
    pub all: Option<bool>,
    pub masternode: Option<String>,
}

#[ocean_endpoint]
async fn list_gov_proposals(
    Query(query): Query<GovernanceQuery>,
    Extension(services): Extension<Arc<Services>>,
) -> Result<ApiPagedResponse<ProposalInfo>> {
    let size = match query.all {
        Some(true) => 0,
        _ => query.pagination.size,
    };

    let opts = ListProposalsOptions {
        pagination: Some(ListProposalsPagination {
            limit: Some(size),
            ..ListProposalsPagination::default()
        }),
        status: query.status,
        r#type: query.r#type,
        cycle: query.cycle,
    };
    let proposals = services.client.list_gov_proposals(Some(opts))?;

    Ok(ApiPagedResponse::of(proposals, size, |proposal| {
        proposal.proposal_id.to_string()
    }))
}

#[ocean_endpoint]
async fn get_gov_proposal(
    Path(proposal_id): Path<String>,
    Extension(services): Extension<Arc<Services>>,
) -> Result<ProposalInfo> {
    let txid: Txid = proposal_id
        .parse()
        .map_err(|_| OceanError::NotFound(NotFoundKind::Proposal))?;

    let proposal = services.client.get_gov_proposal(txid)?;
    Ok(proposal)
}

#[ocean_endpoint]
async fn list_gov_proposal_votes(
    Path(proposal_id): Path<String>,
    Query(query): Query<GovernanceQuery>,
    Extension(services): Extension<Arc<Services>>,
) -> Result<ApiPagedResponse<ListVotesResult>> {
    let proposal_id: Txid = proposal_id
        .parse()
        .map_err(|_| OceanError::NotFound(NotFoundKind::Proposal))?;

    let size = match query.all {
        Some(true) => 0,
        _ => query.pagination.size,
    };

    let start = query
        .pagination
        .next
        .map(|v| v.parse::<usize>())
        .transpose()?;

    let opts = ListGovProposalVotesOptions {
        proposal_id: Some(proposal_id),
        masternode: query.masternode,
        pagination: Some(ListGovProposalVotesPagination {
            limit: Some(size),
            start,
            ..ListGovProposalVotesPagination::default()
        }),
        cycle: query.cycle,
        aggregate: None,
        valid: None,
    };
    let votes = services.client.list_gov_proposal_votes(Some(opts))?;
    let len = votes.len();
    Ok(ApiPagedResponse::of(votes, size, |_| {
        if let Some(next) = start {
            return next + len;
        } else {
            len - 1
        }
    }))
}

pub fn router(services: Arc<Services>) -> Router {
    Router::new()
        .route("/proposals", get(list_gov_proposals))
        .route("/proposals/:id", get(get_gov_proposal))
        .route("/proposals/:id/votes", get(list_gov_proposal_votes))
        .layer(Extension(services))
}
