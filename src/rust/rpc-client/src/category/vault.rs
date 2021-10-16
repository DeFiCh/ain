use crate::Client;
extern crate serde;

use anyhow::Result;
use serde::{Deserialize, Serialize};
use serde_json::json;

#[derive(Debug, Serialize, Deserialize)]
pub struct BatchInfo {
    index: u32,
    collaterals: Vec<String>,
    loan: String,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct VaultInfo {
    pub vault_id: String,
    pub loan_scheme_id: String,
    pub owner_address: String,
    pub is_under_liquidation: bool,

    // keys when is_under_liquidation == false
    pub collateral_amounts: Option<Vec<String>>,
    pub loan_amount: Option<Vec<String>>,
    pub collateral_value: Option<f64>,
    pub loan_value: Option<f64>,
    pub current_ratio: Option<i64>,

    // key when is_under_liquidation == true
    pub batches: Option<Vec<BatchInfo>>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ListVaultInfo {
    pub vault_id: String,
    pub loan_scheme_id: String,
    pub owner_address: String,
    pub is_under_liquidation: bool,
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
    ) -> Result<VaultInfo> {
        self.call::<VaultInfo>(
            "updatevault",
            &[
                vault_id.into(),
                json!({"ownerAddress":new_address, "loanSchemeId": loan_scheme_id}),
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

    pub fn auction_bid(
        &self,
        vault_id: &str,
        index: u32,
        from: &str,
        amount: &str,
    ) -> Result<String> {
        self.call::<String>(
            "auctionbid",
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
}
