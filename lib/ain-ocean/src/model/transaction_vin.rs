use bitcoin::Txid;
use defichain_rpc::json::blockchain::Vin;
use serde::{Deserialize, Serialize};

use super::TransactionVout;

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionVin {
    pub id: String,
    pub txid: Txid,
    pub coinbase: Option<String>,
    pub vout: Option<TransactionVinVout>,
    pub script: Option<String>,
    pub tx_in_witness: Option<Vec<String>>,
    pub sequence: i64,
}

impl TransactionVin {
    pub fn from_vin_and_txid(vin: Vin, txid: Txid, vouts: &Vec<TransactionVout>) -> Self {
        match vin {
            Vin::Coinbase(v) => Self {
                id: format!("{}00", txid),
                txid: txid,
                coinbase: Some(v.coinbase),
                sequence: v.sequence,
                vout: None,
                script: None,
                tx_in_witness: None,
            },
            Vin::Standard(v) => {
                let vout = vouts.get(v.vout as usize).map(|vout| TransactionVinVout {
                    txid: vout.txid,
                    value: vout.value,
                    n: vout.n,
                    token_id: vout.token_id,
                    script: vout.script.hex.to_owned(),
                });
                Self {
                    id: format!("{}{}{:x}", txid, v.txid, v.vout),
                    txid: txid,
                    sequence: v.sequence,
                    vout,
                    script: v.script_sig.hex,
                    tx_in_witness: v.txinwitness,
                    coinbase: None,
                }
            }
        }
    }
}

#[derive(Debug, Serialize, Deserialize)]
pub struct TransactionVinVout {
    pub txid: Txid,
    pub n: usize,
    pub value: f64,
    pub token_id: u8,
    pub script: Vec<u8>,
}
