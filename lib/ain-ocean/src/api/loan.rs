use axum::{extract::Path, routing::get, Router};

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
    Path((vault_id, height, batch_index)): Path<(String, i64, i64)>,
) -> String {
    format!(
        "Auction history for vault id {}, height {}, batch index {}",
        vault_id, height, batch_index
    )
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
