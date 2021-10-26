mod chain_params_base;
mod commands;

use anyhow::Result;
use chain_params_base::Chain;
use commands::{
    AuctionBidCmd, CreateAuctionCmd, CreateCollateralTokenCmd, CreateLoanSchemeCmd,
    CreateLoanTokenCmd, CreateVaultCmd, RefreshOraclesCmd,
};
use defi_rpc::Client;
use structopt::StructOpt;

#[derive(StructOpt, Debug)]
#[structopt(name = "loan-cli")]
pub struct Opt {
    /// RPC user
    #[structopt(short, long)]
    user: Option<String>,

    /// RPC password
    #[structopt(short, long)]
    password: Option<String>,
    /// Specific chain to run.
    #[structopt(
        short,
        long,
        default_value = "regtest",
        possible_values = &["main", "testnet", "regtest", "devnet"]
      )]
    pub chain: Chain,
    #[structopt(subcommand)]
    pub command: Command,
}

#[derive(Debug, StructOpt)]
#[structopt(rename_all = "lower_case")]
pub enum Command {
    CreateLoanToken(CreateLoanTokenCmd),
    CreateCollateralToken(CreateCollateralTokenCmd),
    CreateLoanScheme(CreateLoanSchemeCmd),
    CreateAuction(CreateAuctionCmd),
    CreateVault(CreateVaultCmd),
    AuctionBid(AuctionBidCmd),
    RefreshOracles(RefreshOraclesCmd),
}

fn main() -> Result<()> {
    let opt = Opt::from_args();

    let user = opt.user.unwrap_or(String::from("cake"));
    let password = opt.password.unwrap_or(String::from("cake"));
    let chain_params = opt.chain.get_params();

    let client = Client::new(
        &format!("http://localhost:{}", chain_params.rpc_port),
        user,
        password,
        String::from(chain_params.network),
    )?;

    match opt.command {
        Command::CreateLoanToken(cmd) => cmd.run(&client),
        Command::CreateCollateralToken(cmd) => cmd.run(&client),
        Command::CreateLoanScheme(cmd) => cmd.run(&client),
        Command::CreateAuction(cmd) => cmd.run(&client),
        Command::CreateVault(cmd) => cmd.run(&client),
        Command::AuctionBid(cmd) => cmd.run(&client),
        Command::RefreshOracles(cmd) => cmd.run(&client),
    }
}
