use ain_db::{Column, ColumnName, DBError, TypedColumn};
use anyhow::format_err;
use bitcoin::{hashes::Hash, Txid};

use crate::model;

#[derive(Debug)]
pub struct VaultAuctionHistory;

impl ColumnName for VaultAuctionHistory {
    const NAME: &'static str = "vault_auction_history";
}

impl Column for VaultAuctionHistory {
    type Index = model::AuctionHistoryKey;
}

impl TypedColumn for VaultAuctionHistory {
    type Type = model::VaultAuctionBatchHistory;
}

// Secondary index by block height and txno
#[derive(Debug)]
pub struct VaultAuctionHistoryByHeight;

impl ColumnName for VaultAuctionHistoryByHeight {
    const NAME: &'static str = "vault_auction_history_by_height";
}

impl Column for VaultAuctionHistoryByHeight {
    type Index = model::AuctionHistoryByHeightKey;
}

impl TypedColumn for VaultAuctionHistoryByHeight {
    type Type = model::AuctionHistoryKey;
}
