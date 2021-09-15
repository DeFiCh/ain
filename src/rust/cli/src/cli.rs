use crate::chain_params_base::Chain;
use crate::commands::rpc_api::RpcMethod;
use structopt::StructOpt;

#[derive(StructOpt, Debug)]
#[structopt(name = "defi-cli")]
#[structopt(version = env!("CARGO_PKG_VERSION"))]
#[structopt(about = env!("CARGO_PKG_DESCRIPTION"))]
#[structopt(author = env!("CARGO_PKG_AUTHORS"))]
pub struct Opt {
    /// Activate debug mode
    #[structopt(short, long)]
    debug: bool,

    /// Verbose mode (-v, -vv, -vvv, etc.)
    #[structopt(short, long, parse(from_occurrences))]
    verbose: u8,

    /// Username for JSON-RPC connections.
    #[structopt(long)]
    pub rpcuser: String,

    /// Password for JSON-RPC connections.
    #[structopt(long)]
    pub rpcpassword: String,

    /// Specific chain to run.
    #[structopt(
      short,
      long,
      default_value = "mainnet",
      possible_values = &["testnet", "mainnet", "regtest", "devnet"]
    )]
    pub chain: Chain,

    #[structopt(subcommand)]
    pub command: Command,
}

#[derive(Debug, StructOpt)]
pub enum Command {
    /// Call the RPC API.
    RpcApi(RpcMethod),
}
