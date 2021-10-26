use crate::Client;

use anyhow::Result;
extern crate serde;
use serde::{Deserialize, Serialize};
use serde_json::json;
use std::time::{SystemTime, UNIX_EPOCH};

#[derive(Debug, Serialize, Deserialize)]
pub struct PriceFeed {
    pub token: String,
    pub currency: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct TokenPrice {
    pub token: String,
    pub currency: String,
    pub amount: f64,
    pub timestamp: u64,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct OracleData {
    pub weightage: i64,
    pub oracleid: String,
    pub address: String,
    pub price_feeds: Vec<PriceFeed>,
    pub token_prices: Vec<TokenPrice>,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ListFixedIntervalPriceData {
    pub price_feed_id: String,
    pub active_price: f64,
    pub next_price: f64,
    pub timestamp: u64,
    pub is_valid: bool,
}

#[derive(Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct GetFixedIntervalPriceData {
    pub fixed_interval_price_id: String,
    pub active_price: f64,
    pub next_price: f64,
    pub timestamp: u64,
    pub is_valid: bool,
    pub active_price_block: u64,
    pub next_price_block: u64,
}

impl Client {
    pub fn create_oracle(&self, symbol: &str, amount: f32) -> Result<String> {
        println!("Appointing oracle for token {}", symbol);
        let oracle_address = self.get_new_address()?;
        let price_feed = json!([json!({ "currency": "USD", "token": symbol })]);
        let oracle_id = self.appoint_oracle(&oracle_address, price_feed, 1)?;
        self.await_n_confirmations(&oracle_id, 1)?;

        let oracle_prices =
            json!([json!({ "currency": "USD", "tokenAmount": format!("{}@{}", amount, symbol) })]);
        let timestamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
        let set_oracle_tx = self.call::<String>(
            "setoracledata",
            &[oracle_id.clone().into(), timestamp.into(), oracle_prices],
        )?;
        self.await_n_confirmations(&set_oracle_tx, 1)?;

        Ok(oracle_id)
    }

    // Creates a price oracle for rely of real time price data.
    pub fn appoint_oracle(
        &self,
        oracle_address: &str,
        price_feed: serde_json::Value,
        weightage: u32,
    ) -> Result<String> {
        self.call::<String>(
            "appointoracle",
            &[oracle_address.into(), price_feed, weightage.into()],
        )
    }

    // Set oracle data transaction.
    pub fn set_oracle_data(&self, oracle_id: &str, token: &str, amount: f32) -> Result<String> {
        let timestamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_secs();
        self.call::<String>(
            "setoracledata",
            &[
                oracle_id.clone().into(),
                timestamp.into(),
                json!([{ "currency": "USD", "tokenAmount": format!("{:.8}@{}", amount, token) }]),
            ],
        )
    }

    // Returns array of oracle ids.
    pub fn list_oracles(&self) -> Result<Vec<String>> {
        self.call::<Vec<String>>("listoracles", &[])
    }

    // Returns oracle data.
    pub fn get_oracle(&self, oracle_id: &str) -> Result<OracleData> {
        self.call::<OracleData>("getoracledata", &[oracle_id.into()])
    }

    pub fn remove_oracle(&self, oracle_id: &str) -> Result<String> {
        self.call::<String>("removeoracle", &[oracle_id.into()])
    }

    // Returns fixed interval price for token/USD.
    pub fn get_fixed_interval_price(&self, token: &str) -> Result<GetFixedIntervalPriceData> {
        self.call::<GetFixedIntervalPriceData>(
            "getfixedintervalprice",
            &[format!("{}/USD", token).into()],
        )
    }

    // List all fixed interval prices.
    pub fn list_fixed_interval_prices(&self) -> Result<Vec<ListFixedIntervalPriceData>> {
        self.call::<Vec<ListFixedIntervalPriceData>>("listfixedintervalprices", &[])
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn create_and_remove_oracle() -> Result<()> {
        let client = Client::from_env()?;
        let oracle_id = client.create_oracle("DFI", 1.);
        assert!(oracle_id.is_ok());
        client.remove_oracle(&oracle_id.unwrap())?;
        Ok(())
    }
}
