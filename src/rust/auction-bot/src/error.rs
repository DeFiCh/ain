#[derive(Debug)]
pub enum BidError {
    Anyhow(anyhow::Error),
    NoBids,
    NoBatches,
}

impl From<anyhow::Error> for BidError {
    fn from(err: anyhow::Error) -> Self {
        Self::Anyhow(err)
    }
}
