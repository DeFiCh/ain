use crate::client::client::Client;
use crate::error::*;
use structopt::StructOpt;

use super::block::{GetBlockCountCmd, GetBlockHashCmd};

#[derive(Debug, StructOpt)]
#[structopt(rename_all = "lower_case")]
pub enum RpcMethod {
    GetBlockCount(GetBlockCountCmd),
    GetBlockHash(GetBlockHashCmd),
}

impl RpcMethod {
    pub fn run(&self, client: &Client) -> Result<(), Error> {
        match self {
            RpcMethod::GetBlockCount(cmd) => cmd.run(client),
            RpcMethod::GetBlockHash(cmd) => cmd.run(client),
        }
    }
}
