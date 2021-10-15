use crate::Client;
extern crate serde;

use anyhow::Result;
use serde::{Deserialize, Serialize};
use serde_json::json;

#[derive(Debug, Serialize, Deserialize)]
pub struct VaultInfo {
    #[serde(rename = "vaultId")]
    pub vault_id: String,
    #[serde(rename = "loanSchemeId")]
    pub loan_scheme_id: String,
    #[serde(rename = "ownerAddress")]
    pub owner_address: String,
    #[serde(rename = "isUnderLiquidation")]
    pub is_under_liquidation: bool,
    #[serde(rename = "collateralAmounts")]
    pub collateral_amounts: Vec<String>,
    #[serde(rename = "loanAmount")]
    pub loan_amount: Vec<String>,
    #[serde(rename = "collateralValue")]
    pub collateral_value: f64,
    #[serde(rename = "loanValue")]
    pub loan_value: f64,
    #[serde(rename = "currentRatio")]
    pub current_ratio: i64,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ListVaultInfo {
    #[serde(rename = "vaultId")]
    pub vault_id: String,
    #[serde(rename = "loanSchemeId")]
    pub loan_scheme_id: String,
    #[serde(rename = "ownerAddress")]
    pub owner_address: String,
    #[serde(rename = "isUnderLiquidation")]
    pub is_under_liquidation: bool,
}

impl Client {
    pub fn create_vault(&self, address: &str, loan_scheme: &str) -> Result<String> {
        self.call::<String>("createvault", &[address.into(), loan_scheme.into()])
    }

    pub fn get_vault(&self, vault_id: &str) -> Result<VaultInfo> {
        self.call::<VaultInfo>("getvault", &[vault_id.clone().into()])
    }

    pub fn list_vaults(&self, owner_address: Option<&str>) -> Result<Vec<ListVaultInfo>> {
        let mut args: Vec<serde_json::Value> = Vec::new();
        if let Some(owner_address) = owner_address {
            args.push(json!({ "ownerAddress": owner_address }));
        }
        self.call::<Vec<ListVaultInfo>>("listvaults", &args)
    }

    pub fn deposit_to_vault(&self, vault_id: &str, address: &str, amount: &str) -> Result<String> {
        self.call::<String>(
            "deposittovault",
            &[vault_id.into(), address.into(), amount.into()],
        )
    }
}
