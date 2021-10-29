use crate::Client;
extern crate serde;

use anyhow::Result;
use serde::Deserializer;
use serde::{Deserialize, Serialize};
use serde_json::json;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BidInfo {
    pub owner: String,
    pub amount: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BatchInfo {
    pub index: u32,
    pub collaterals: Vec<String>,
    pub loan: String,
    #[serde(rename = "highestBid")]
    pub highest_bid: Option<BidInfo>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ListVaultInfo {
    pub vault_id: String,
    pub loan_scheme_id: String,
    pub owner_address: String,
    pub state: String,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AuctionInfo {
    pub vault_id: String,
    pub liquidation_height: i64,
    pub batch_count: i64,
    pub liquidation_penalty: f64,
    pub batches: Vec<BatchInfo>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AuctionHistory {
    pub winner: String,
    pub block_height: u64,
    pub block_hash: String,
    pub block_time: u64,
    pub vault_id: String,
    pub batch_index: u32,
    pub auction_bid: String,
    pub auction_won: Vec<String>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct VaultData {
    pub vault_id: String,
    pub loan_scheme_id: String,
    pub owner_address: String,
    pub collateral_amounts: Vec<String>,
    pub loan_amounts: Vec<String>,
    pub interest_amounts: Vec<String>,
    pub collateral_value: f64,
    pub loan_value: f64,
    #[serde(deserialize_with = "ok_or_default")]
    pub interest_value: f64,
    pub current_ratio: f64,
}

fn ok_or_default<'de, T, D>(deserializer: D) -> Result<T, D::Error>
where
    T: Deserialize<'de> + Default,
    D: Deserializer<'de>,
{
    Ok(Deserialize::deserialize(deserializer).unwrap_or_default())
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ActiveVaultInValidPrice {
    pub vault_id: String,
    pub loan_scheme_id: String,
    pub owner_address: String,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct LiquidatedVaultData {
    pub vault_id: String,
    pub loan_scheme_id: String,
    pub owner_address: String,
    pub batches: Vec<BatchInfo>,
    pub batch_count: u8,
    pub liquidation_height: u32,
    pub liquidation_penalty: f32,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(tag = "state")]
pub enum VaultInfo {
    #[serde(rename = "active")]
    Active(VaultData),
    #[serde(rename = "frozen")]
    Frozen(VaultData),
    #[serde(rename = "inliquidation")]
    InLiquidation(LiquidatedVaultData),
}

impl Client {
    pub fn create_vault(&self, address: &str, loan_scheme: &str) -> Result<String> {
        self.call::<String>("createvault", &[address.into(), loan_scheme.into()])
    }

    pub fn get_vault(&self, vault_id: &str) -> Result<VaultInfo> {
        self.call::<VaultInfo>("getvault", &[vault_id.into()])
    }

    pub fn update_vault(
        &self,
        vault_id: &str,
        new_address: &str,
        loan_scheme_id: &str,
    ) -> Result<String> {
        self.call::<String>(
            "updatevault",
            &[
                vault_id.into(),
                json!({ "ownerAddress":new_address, "loanSchemeId": loan_scheme_id }),
            ],
        )
    }

    pub fn deposit_to_vault(&self, vault_id: &str, address: &str, amount: &str) -> Result<String> {
        self.call::<String>(
            "deposittovault",
            &[vault_id.into(), address.into(), amount.into()],
        )
    }

    pub fn withdraw_from_vault(
        &self,
        vault_id: &str,
        to_address: &str,
        amount: &str,
    ) -> Result<String> {
        self.call::<String>(
            "withdrawfromvault",
            &[vault_id.into(), to_address.into(), amount.into()],
        )
    }

    pub fn close_vault(&self, vault_id: &str, to_address: &str) -> Result<VaultInfo> {
        self.call::<VaultInfo>("closevault", &[vault_id.into(), to_address.into()])
    }

    pub fn list_vaults(&self, owner_address: Option<&str>) -> Result<Vec<ListVaultInfo>> {
        let mut args: Vec<serde_json::Value> = Vec::new();
        if let Some(owner_address) = owner_address {
            args.push(json!({ "ownerAddress": owner_address }));
        }
        self.call::<Vec<ListVaultInfo>>("listvaults", &args)
    }

    pub fn list_auctions(&self) -> Result<Vec<AuctionInfo>> {
        self.call::<Vec<AuctionInfo>>("listauctions", &[])
    }

    // List auction history. owner_address defaults to "mine"
    pub fn list_auction_history(&self, owner_address: Option<&str>) -> Result<Vec<AuctionHistory>> {
        let address = owner_address.unwrap_or("mine");
        self.call::<Vec<AuctionHistory>>("listauctionhistory", &[address.into()])
    }

    pub fn auction_bid(
        &self,
        vault_id: &str,
        index: u32,
        from: &str,
        amount: &str,
    ) -> Result<String> {
        self.call::<String>(
            "placeauctionbid",
            &[vault_id.into(), index.into(), from.into(), amount.into()],
        )
    }
}

#[cfg(test)]
mod test {
    use super::*;
    #[test]
    fn list_vaults() -> Result<()> {
        let client = Client::from_env()?;
        client.list_vaults(None)?;
        Ok(())
    }

    #[test]
    fn list_auction_history() -> Result<()> {
        let client = Client::from_env()?;
        client.list_auction_history(None)?;
        Ok(())
    }
}
