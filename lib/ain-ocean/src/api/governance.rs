use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use bitcoin::Txid;
use defichain_rpc::{json::governance::*, GovernanceRPC};
use serde::Deserialize;

use super::{
    path::Path,
    query::{PaginationQuery, Query},
    response::{ApiPagedResponse, Response},
    AppContext,
};
use crate::{
    error::{ApiError, Error, NotFoundKind},
    model::ApiProposalInfo,
    Result,
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
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<ApiProposalInfo>> {
    let size = match query.all {
        Some(true) => 0,
        _ => query.pagination.size,
    };

    let opts = ListProposalsOptions {
        pagination: Some(ListProposalsPagination {
            limit: Some(size),
            start: query.pagination.next.clone(),
            ..ListProposalsPagination::default()
        }),
        status: query.status,
        r#type: query.r#type,
        cycle: query.cycle,
    };
    let proposals = ctx.client.list_gov_proposals(Some(opts)).await?;
    let mut proposals_with_string_amount: Vec<ApiProposalInfo> =
        proposals.into_iter().map(ApiProposalInfo::from).collect();
    // proposals.sort_by(|a, b| a.creation_height.cmp(&b.creation_height));
    proposals_with_string_amount.sort_by(|a, b| a.creation_height.cmp(&b.creation_height));
    Ok(ApiPagedResponse::of(
        proposals_with_string_amount,
        size,
        |proposal| proposal.proposal_id.to_string(),
    ))
}

#[ocean_endpoint]
async fn get_gov_proposal(
    Extension(ctx): Extension<Arc<AppContext>>,
    Path(proposal_id): Path<String>,
) -> Result<Response<ApiProposalInfo>> {
    let txid: Txid = proposal_id
        .parse()
        .map_err(|_| Error::NotFound(NotFoundKind::Proposal))?;

    let proposal = ctx.client.get_gov_proposal(txid).await?;
    Ok(Response::new(proposal.into()))
}

#[ocean_endpoint]
async fn list_gov_proposal_votes(
    Path(proposal_id): Path<String>,
    Query(query): Query<GovernanceQuery>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<ApiPagedResponse<ListVotesResult>> {
    let proposal_id: Txid = proposal_id
        .parse()
        .map_err(|_| Error::NotFound(NotFoundKind::Proposal))?;

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
    let votes = ctx.client.list_gov_proposal_votes(Some(opts)).await?;
    let len = votes.len();
    Ok(ApiPagedResponse::of(votes, size, |_| {
        if let Some(next) = start {
            next + len
        } else {
            len - 1
        }
    }))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/proposals", get(list_gov_proposals))
        .route("/proposals/:id", get(get_gov_proposal))
        .route("/proposals/:id/votes", get(list_gov_proposal_votes))
        .layer(Extension(ctx))
}
