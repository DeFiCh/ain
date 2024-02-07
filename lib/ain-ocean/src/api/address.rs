use std::sync::Arc;

use axum::{extract::Path, routing::get, Router};
use defichain_rpc::{Client, RpcApi};
use serde::Deserialize;

#[derive(Deserialize)]
struct Address {
    address: String,
}

#[derive(Deserialize)]
struct History {
    address: String,
    height: i64,
    txno: i64,
}

async fn get_account_history(
    Path(History {
        address,
        height,
        txno,
    }): Path<History>,
) -> String {
    format!(
        "Account history for address {}, height {}, txno {}",
        address, height, txno
    )
}

async fn list_account_history(Path(Address { address }): Path<Address>) -> String {
    format!("List account history for address {}", address)
}

async fn get_balance(Path(Address { address }): Path<Address>) -> String {
    format!("balance for address {address}")
}

async fn get_aggregation(Path(Address { address }): Path<Address>) -> String {
    format!("Aggregation for address {}", address)
}

async fn list_token(Path(Address { address }): Path<Address>) -> String {
    format!("List tokens for address {}", address)
}

async fn list_vault(Path(Address { address }): Path<Address>) -> String {
    format!("List vaults for address {}", address)
}

async fn list_transaction(Path(Address { address }): Path<Address>) -> String {
    format!("List transactions for address {}", address)
}

async fn list_transaction_unspent(Path(Address { address }): Path<Address>) -> String {
    format!("List unspent transactions for address {}", address)
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new().nest(
        "/:address",
        Router::new()
            .route("/history/:height/:txno", get(get_account_history))
            .route("/history", get(list_account_history))
            .route("/balance", get(get_balance))
            .route("/aggregation", get(get_aggregation))
            .route("/tokens", get(list_token))
            .route("/vaults", get(list_vault))
            .route("/transactions", get(list_transaction))
            .route("/transactions/unspent", get(list_transaction_unspent)),
    )
}
