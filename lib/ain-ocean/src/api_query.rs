use serde::Deserialize;

#[derive(Deserialize)]
pub struct PaginationQuery {
    pub size: usize,
    pub next: Option<String>,
}
