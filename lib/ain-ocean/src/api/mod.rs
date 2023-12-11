use axum::Router;

mod address;
mod block;
mod fee;
mod governance;
mod loan;
mod masternode;
mod oracle;
mod poolpairs;
mod prices;
mod rawtx;
mod stats;
mod tokens;
mod transactions;

pub fn ocean_router() -> Router {
    Router::new()
        .nest("/address", address::router())
        .nest("/governance", governance::router())
        .nest("/loans", loan::router())
        .nest("/fee", fee::router())
        .nest("/masternodes", masternode::router())
        .nest("/oracles", oracle::router())
        .nest("/poolpairs", poolpairs::router())
        .nest("/prices", prices::router())
        .nest("/rawtx", rawtx::router())
        .nest("/stats", stats::router())
        .nest("/tokens", tokens::router())
        .nest("/transactions", transactions::router())
        .nest("/blocks", block::router())
}