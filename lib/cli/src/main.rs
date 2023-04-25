mod command;
mod format;
mod params;
mod result;
mod structs;

use crate::structs::CallRequest;
use ain_grpc::block::BlockNumber;
use command::execute_cli_command;
use format::Format;
use jsonrpsee::http_client::HttpClientBuilder;
use params::{BaseChainParams, Chain};
use primitive_types::{H160, H256, U256};
use structopt::StructOpt;

#[derive(Debug, StructOpt)]
#[structopt(name = "metachain-cli", about = "Metachain JSON-RPC CLI")]
struct Opt {
    /// The chain to be used. Defaults to mainnet
    #[structopt(short, long, default_value = "main")]
    chain: Chain,

    /// The chain to be used. Defaults to mainnet
    #[structopt(long, default_value = "json")]
    format: Format,

    #[structopt(subcommand)]
    cmd: MetachainCLI,
}

#[derive(Debug, StructOpt)]
pub enum MetachainCLI {
    /// Returns a list of accounts owned by the client.
    Accounts,
    /// Returns the chain ID used by this client.
    ChainId,
    /// Returns the current network version.
    NetVersion,
    /// Returns whether the client is mining or not.
    Mining,
    /// Executes a new message call immediately without creating a transaction on the block chain.
    Call {
        #[structopt(flatten)]
        input: Box<CallRequest>,
    },
    /// Returns the balance of the account of the given address.
    GetBalance {
        #[structopt(parse(try_from_str))]
        address: H160,
    },
    /// Returns information about a block by its hash.
    GetBlockByHash {
        #[structopt(parse(try_from_str))]
        hash: H256,
    },
    /// Returns the number of hashes per second the node is mining with.
    HashRate,
    /// Returns the number of most recent block.
    BlockNumber,
    /// Returns information about a block by its block number.
    GetBlockByNumber {
        #[structopt(parse(try_from_str))]
        block_number: BlockNumber,
        #[structopt(long)]
        full_transaction: bool,
    },
    /// Returns the information about a transaction requested by its transaction hash.
    GetTransactionByHash {
        #[structopt(parse(try_from_str))]
        hash: H256,
    },
    /// Returns information about a transaction by block hash and transaction index position.
    GetTransactionByBlockHashAndIndex {
        #[structopt(parse(try_from_str))]
        hash: H256,
        index: usize,
    },
    /// Returns information about a transaction by block number and transaction index position.
    GetTransactionByBlockNumberAndIndex {
        #[structopt(parse(try_from_str))]
        block_number: U256,
        index: usize,
    },
    /// Returns the number of transactions in a block from a block matching the given block hash.
    GetBlockTransactionCountByHash {
        #[structopt(parse(try_from_str))]
        hash: H256,
    },
    /// Returns the number of transactions in a block matching the given block number.
    GetBlockTransactionCountByNumber {
        #[structopt(parse(try_from_str))]
        number: BlockNumber,
    },
    /// Returns the code at a given address.
    GetCode {
        #[structopt(parse(try_from_str))]
        address: H160,
    },
    /// Returns the value from a storage position at a given address.
    GetStorageAt {
        #[structopt(parse(try_from_str))]
        address: H160,
        #[structopt(parse(try_from_str))]
        position: H256,
    },
    /// Sends a signed transaction and returns the transaction hash.
    SendRawTransaction { input: String },
    /// Returns the number of transactions sent from an address.
    GetTransactionCount {
        #[structopt(parse(try_from_str))]
        input: H160,
    },
    /// Generates and returns an estimate of how much gas is necessary to allow the transaction to complete.
    EstimateGas,
    /// Returns an object representing the current state of the Metachain network.
    GetState,
    /// Returns the current price per gas in wei.
    GasPrice,
}

#[tokio::main]
async fn main() -> Result<(), jsonrpsee::core::Error> {
    let opt = Opt::from_args();

    let client = {
        let chain = opt.chain;
        let base_chain_params = BaseChainParams::create(&chain);
        let json_addr = format!("http://127.0.0.1:{}", base_chain_params.eth_rpc_port);
        HttpClientBuilder::default().build(json_addr)
    }?;

    let result = execute_cli_command(opt.cmd, &client).await?;
    match opt.format {
        Format::Rust => println!("{}", result),
        Format::Json => println!("{}", serde_json::to_string(&result)?),
        Format::PrettyJson => println!("{}", serde_json::to_string_pretty(&result)?),
    };
    Ok(())
}
