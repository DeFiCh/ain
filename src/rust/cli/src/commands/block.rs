use crate::client::client::Client;
use crate::error::*;
use structopt::StructOpt;

#[derive(Debug, StructOpt)]
#[structopt(about = "Get the number of block in the longest chain.")]
pub struct GetBlockCountCmd {
    /// Arguments to be passed to the RPC.
    #[structopt(long)]
    pub args: Vec<String>,
}

impl GetBlockCountCmd {
    pub fn run(&self, client: &Client) -> Result<(), Error> {
        match client.get_block_count() {
            Ok(data) => {
                println!("{}", data);
                Ok(())
            }
            Err(e) => Err(Error::from(e)),
        }
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
        let res = if let Some(height) = self.height {
            client.get_block_hash(height)
        } else {
            client.get_best_block_hash()
        };
        match res {
            Ok(data) => {
                println!("{}", data);
                Ok(())
            }
            Err(e) => Err(Error::from(e)),
        }
    }
}
