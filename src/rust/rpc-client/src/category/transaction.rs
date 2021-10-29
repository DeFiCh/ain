use crate::Client;
extern crate serde;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionResult {
    pub amount: f64,
    pub fee: f64,
    pub confirmations: i64,
    pub trusted: Option<bool>,
    pub blockhash: Option<String>,
    pub blockindex: Option<u64>,
    pub blocktime: Option<u64>,
    pub txid: String,
    pub walletconflicts: Vec<Option<serde_json::Value>>,
    pub time: u64,
    pub timereceived: u64,
    #[serde(rename = "bip125-replaceable")]
    pub bip125_replaceable: String,
    pub details: Vec<Detail>,
    pub hex: String,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct Detail {
    pub category: String,
    pub amount: f64,
    pub vout: i64,
    pub fee: Option<f64>,
    pub abandoned: Option<bool>,
    pub address: Option<String>,
    pub label: Option<String>,
}

impl Client {
    pub fn await_n_confirmations(&self, tx_hash: &str, n_confirmations: i64) -> Result<()> {
        for i in 0..1320 {
            let tx_info = self.call::<TransactionResult>("gettransaction", &[tx_hash.into()])?;
            if tx_info.confirmations < n_confirmations {
                std::thread::sleep(std::time::Duration::from_secs(1));
            } else {
                return Ok(());
            }
            if i % 60 == 0 {
                println!("Been waiting on tx {} for {} seconds...", tx_hash, i);
            }
        }
        Err(anyhow!(
            "Transaction {} did not reach {} confirmations.",
            tx_hash,
            n_confirmations
        ))
    }
}
