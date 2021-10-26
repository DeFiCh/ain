extern crate serde;
use crate::Client;
use anyhow::Result;
use serde::{Deserialize, Serialize};

use std::collections::HashMap;
pub type GovVar = Vec<GovType>;

#[derive(Debug, Serialize, Deserialize)]
#[serde(untagged)]
pub enum GovType {
    Amount(HashMap<String, f32>),
    Split(HashMap<String, HashMap<String, f32>>),
}

impl Client {
    pub fn get_gov(&self, name: &str) -> Result<GovVar> {
        self.call::<GovVar>("getgov", &[name.into()])
    }

    pub fn list_gov(&self) -> Result<Vec<GovVar>> {
        self.call::<Vec<GovVar>>("listgovs", &[])
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn list_govs() -> Result<()> {
        let client = Client::from_env()?;
        client.list_gov()?;
        Ok(())
    }

    #[test]
    fn get_gov() -> Result<()> {
        let client = Client::from_env()?;
        client.get_gov("LP_DAILY_DFI_REWARD")?;
        Ok(())
    }
}
