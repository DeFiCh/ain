mod chain_params_base;
mod cli;
mod client;
mod commands;
mod error;

use cli::Command;
use cli::Opt;
use client::client::{Auth, Client};
use structopt::StructOpt;

const DEFAULT_RPCCONNECT: &str = "127.0.0.1";

fn main() -> Result<(), error::Error> {
    let opt = Opt::from_args();
    let chain_params = opt.chain.get_params();

    let client = match Client::new(
        &format!("http://{}:{}", DEFAULT_RPCCONNECT, chain_params.rpc_port),
        Auth::UserPass(opt.rpcuser.to_string(), opt.rpcpassword.to_string()),
    ) {
        Ok(client) => client,
        Err(error) => panic!("Error creating RPC client : {:?}", error),
    };

    match opt.command {
        Command::RpcApi(cmd) => cmd.run(&client),
    }
}
