use anyhow::Result;
use defi_rpc::Client;
use structopt::StructOpt;

#[derive(Debug, StructOpt)]
#[structopt(about = "Refreshes oracle prices.")]
pub struct RefreshOraclesCmd {}

impl RefreshOraclesCmd {
    pub fn run(&self, client: &Client) -> Result<()> {
        let list_oracles = client.list_oracles()?;
        for oracle in list_oracles {
            let oracle_data = client.get_oracle(&oracle)?;
            let token = &oracle_data.price_feeds[0].token;
            if let Some(amount) = oracle_data.token_prices.get(0) {
                client.set_oracle_data(&oracle_data.oracleid, token, amount.amount as f32)?;
            }
        }
        Ok(())
    }
}
