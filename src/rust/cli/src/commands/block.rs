use crate::client::client::Client;
use crate::error::*;
use structopt::StructOpt;

#[derive(Debug, StructOpt)]
#[structopt(about = "Get the number of block in the longest chain.")]
pub struct GetBlockCountCmd {}

impl GetBlockCountCmd {
    pub fn run(&self, client: &Client) -> Result<(), Error> {
        let data = client.get_block_count()?;
        println!("{}", data);
        Ok(())
    }
}

#[derive(Debug, StructOpt)]
#[structopt(about = "Get the hash for the given block. Defaults to tip of longest chain.")]
pub struct GetBlockHashCmd {
    /// Block height.
    #[structopt(long)]
    pub height: Option<u64>,
}

impl GetBlockHashCmd {
    pub fn run(&self, client: &Client) -> Result<(), Error> {
        let data = if let Some(height) = self.height {
            client.get_block_hash(height)?
        } else {
            client.get_best_block_hash()?
        };
        println!("{}", data);
        Ok(())
    }
}
