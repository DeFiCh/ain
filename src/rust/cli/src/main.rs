mod chain_params_base;
mod cli;
mod commands;

use anyhow::Result;
use cli::Command;
use cli::Opt;
use defi_rpc::{Auth, Client};
use structopt::StructOpt;

const DEFAULT_HOST: &str = "127.0.0.1";

fn main() -> Result<()> {
    let opt = Opt::from_args();
    let chain_params = opt.chain.get_params();

    let client = Client::new(
        &format!("http://{}:{}", DEFAULT_HOST, chain_params.rpc_port),
        Auth::UserPass(opt.rpcuser.to_string(), opt.rpcpassword.to_string()),
    )?;

    match opt.command {
        Command::RpcApi(cmd) => cmd.run(&client),
    }
}
