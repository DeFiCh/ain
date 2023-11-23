use axum::{extract::Path, routing::get, Router};

async fn list_gov_proposals() -> String {
    "List of governance proposals".to_string()
}

async fn get_gov_proposal(Path(proposal_id): Path<String>) -> String {
    format!("Details of governance proposal with id {}", proposal_id)
}

async fn list_gov_proposal_votes(Path(proposal_id): Path<String>) -> String {
    format!("Votes for governance proposal with id {}", proposal_id)
}

pub fn router() -> Router {
    Router::new()
        .route("/proposals", get(list_gov_proposals))
        .route("/proposals/:id", get(get_gov_proposal))
        .route("/proposals/:id/votes", get(list_gov_proposal_votes))
}
