use axum::{routing::get, Router};

async fn get_stats() -> String {
    "General stats".to_string()
}

async fn get_reward_distribution() -> String {
    "Reward distribution stats".to_string()
}

async fn get_supply() -> String {
    "Supply stats".to_string()
}

async fn get_burn() -> String {
    "Burn stats".to_string()
}

pub fn router() -> Router {
    Router::new()
        .route("/", get(get_stats))
        .route("/rewards/distribution", get(get_reward_distribution))
        .route("/supply", get(get_supply))
        .route("/burn", get(get_burn))
}