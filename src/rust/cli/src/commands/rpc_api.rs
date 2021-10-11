use anyhow::Result;
use defi_rpc::Client;
use structopt::StructOpt;

use super::block::BlockCmd;

#[derive(Debug, StructOpt)]
#[structopt(rename_all = "lower_case")]
pub enum RpcMethod {
    Block(BlockCmd),
}

impl RpcMethod {
    pub fn run(&self, client: &Client) -> Result<()> {
        match self {
            RpcMethod::Block(cmd) => cmd.run(client),
        }
    }
}
