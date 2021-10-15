mod chain_params_base;
mod cli;
mod commands;

use anyhow::Result;
use cli::Command;
use cli::Opt;
use defi_rpc::Client;
use structopt::StructOpt;

fn main() -> Result<()> {
    let opt = Opt::from_args();

    let client = Client::from_env()?;

    match opt.command {
        Command::RpcApi(cmd) => cmd.run(&client),
    }
}
