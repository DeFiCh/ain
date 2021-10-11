use anyhow::Result;
use defi_rpc::Client;
use structopt::StructOpt;

#[derive(Debug, StructOpt)]
#[structopt(about = "Get the number of block in the longest chain.")]
pub struct GetBlockCountCmd {}

impl GetBlockCountCmd {
    pub fn run(&self, client: &Client) -> Result<()> {
        let data = client.call::<u64>("getblockcount", &[])?;
        println!("{}", data);
        Ok(())
    }
}

#[derive(Debug, StructOpt)]
#[structopt(about = "Get the hash for the given block.")]
pub struct GetBlockHashCmd {
    /// Block height.
    #[structopt(long)]
    pub height: u64,
}

impl GetBlockHashCmd {
    pub fn run(&self, client: &Client) -> Result<()> {
        let data = client.call::<String>("getblockhash", &[])?;
        println!("{}", data);
        Ok(())
    }
}

#[derive(Debug, StructOpt)]
#[structopt(about = "Get the hash for the tip of longest chain.")]
pub struct GetBestBlockHashCmd {}

impl GetBestBlockHashCmd {
    pub fn run(&self, client: &Client) -> Result<()> {
        let data = client.call::<String>("getbestblockhash", &[])?;
        println!("{}", data);
        Ok(())
    }
}

#[derive(Debug, StructOpt)]
#[structopt(rename_all = "lower_case")]
pub enum BlockCmd {
    GetBlockCount(GetBlockCountCmd),
    GetBlockHash(GetBlockHashCmd),
    GetBestBlockHash(GetBestBlockHashCmd),
}

impl BlockCmd {
    pub fn run(&self, client: &Client) -> Result<()> {
        match self {
            BlockCmd::GetBlockCount(cmd) => cmd.run(client),
            BlockCmd::GetBlockHash(cmd) => cmd.run(client),
            BlockCmd::GetBestBlockHash(cmd) => cmd.run(client),
        }
    }
}
