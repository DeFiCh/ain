use axum::{
    extract::{Path, Query},
    routing::get,
    Json, Router,
};
use bitcoin::Txid;
use log::debug;

use crate::{
    api_paged_response::ApiPagedResponse, api_query::PaginationQuery,
    model::VaultAuctionBatchHistory, repository::RepositoryOps, Result, SERVICES,
};

async fn list_scheme() -> String {
    "List of loan schemes".to_string()
}

async fn get_scheme(Path(scheme_id): Path<String>) -> String {
    format!("Details of loan scheme with id {}", scheme_id)
}

async fn list_collateral_token() -> String {
    "List of collateral tokens".to_string()
}

async fn get_collateral_token(Path(token_id): Path<String>) -> String {
    format!("Details of collateral token with id {}", token_id)
}

async fn list_loan_token() -> String {
    "List of loan tokens".to_string()
}

async fn get_loan_token(Path(token_id): Path<String>) -> String {
    format!("Details of loan token with id {}", token_id)
}

async fn list_vault() -> String {
    "List of vaults".to_string()
}

async fn get_vault(Path(vault_id): Path<String>) -> String {
    format!("Details of vault with id {}", vault_id)
}

async fn list_vault_auction_history(
    Path((vault_id, height, batch_index)): Path<(Txid, u32, u32)>,
    Query(query): Query<PaginationQuery>,
) -> Result<Json<ApiPagedResponse<VaultAuctionBatchHistory>>> {
    println!("listvault auction history");
    debug!(
        "Auction history for vault id {}, height {}, batch index {}",
        vault_id, height, batch_index
    );
    let next = query
        .next
        .map(|q| {
            let parts: Vec<&str> = q.split('-').collect();
            if parts.len() != 2 {
                return Err("Invalid query format");
            }

            let height = parts[0].parse::<u32>().map_err(|_| "Invalid height")?;
            let txno = parts[1].parse::<usize>().map_err(|_| "Invalid txno")?;

            Ok((height, txno))
        })
        .transpose()?
        .unwrap_or_default();

    debug!("next : {:?}", next);

    let size = if query.size > 0 { query.size } else { 20 };

    let auctions = SERVICES
        .auction
        .by_height
        .list(Some((vault_id, batch_index, next.0, next.1)))?
        .take(size)
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == vault_id && k.1 == batch_index,
            _ => true,
        })
        .map(|item| {
            let (_, id) = item?;

            let auction = SERVICES
                .auction
                .by_id
                .get(&id)?
                .ok_or("Missing auction index")?;

            Ok(auction)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(Json(ApiPagedResponse::of(
        auctions,
        query.size,
        |auction| auction.sort.to_string(),
    )))
}

async fn list_auction() -> String {
    "List of auctions".to_string()
}

pub fn router() -> Router {
    Router::new()
        .route("/schemes", get(list_scheme))
        .route("/schemes/:id", get(get_scheme))
        .route("/collaterals", get(list_collateral_token))
        .route("/collaterals/:id", get(get_collateral_token))
        .route("/tokens", get(list_loan_token))
        .route("/tokens/:id", get(get_loan_token))
        .route("/vaults", get(list_vault))
        .route("/vaults/:id", get(get_vault))
        .route(
            "/vaults/:id/auctions/:height/batches/:batchIndex/history",
            get(list_vault_auction_history),
        )
        .route("/auctions", get(list_auction))
}
