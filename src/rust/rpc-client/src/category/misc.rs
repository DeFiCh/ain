extern crate serde;
use crate::Client;
use anyhow::Result;
use serde_json::json;

impl Client {
    pub fn wait_for_balance_gte(&self, balance: f32) -> Result<()> {
        loop {
            let get_balance = self.call::<f32>("getbalance", &[])?;
            if get_balance < balance {
                println!(
                    "current balance : {}, waiting to reach {}",
                    get_balance, balance,
                );
                println!(
                    "current blockheight : {}",
                    self.call::<u32>("getblockcount", &[])?
                );
                std::thread::sleep(std::time::Duration::from_secs(2));
            } else {
                return Ok(());
            }
        }
    }

    pub fn get_new_address(&self) -> Result<String> {
        self.call::<String>("getnewaddress", &[])
    }

    pub fn utxo_to_account(&self, account: &str, amount: &str) -> Result<String> {
        self.call::<String>("utxostoaccount", &[json!({ account: amount })])
    }

    pub fn send_tokens_to_address(&self, to_address: &str, amount: &str) -> Result<String> {
        self.call::<String>(
            "sendtokenstoaddress",
            &[json!({}), json!({ to_address: amount })],
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn get_new_address() -> Result<()> {
        let client = Client::from_env()?;
        client.get_new_address()?;
        Ok(())
    }
}
