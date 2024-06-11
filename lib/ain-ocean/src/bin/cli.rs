use std::{
    net::SocketAddr,
    path::PathBuf,
    sync::Arc,
    thread,
    time::{Duration, Instant},
};

use ain_ocean::{
    index_block, network::Network, storage::ocean_store::OceanStore, PoolCreationHeight, Result,
    Services,
};
use clap::Parser;
use defichain_rpc::{json::blockchain::*, Auth, BlockchainRPC, Client, PoolPairRPC};

#[derive(Parser, Debug)]
#[command(
    author = "Defichain Labs <engineering@defichain.com>",
    version = "0.1",
    about = "Runs the Ocean Node Service"
)]
struct Cli {
    /// Sets a custom data directory
    #[arg(long, value_name = "DATADIR")]
    datadir: PathBuf,

    /// Sets the RPC server address
    #[arg(long, value_name = "RPCADDRESS")]
    rpcaddress: String,

    /// Sets the RPC username
    #[arg(long, value_name = "USERNAME")]
    user: String,

    /// Sets the RPC password
    #[arg(long, value_name = "PASSWORD")]
    pass: String,

    /// Sets the bind address for the TCP listener
    #[arg(long, value_name = "BINDADDRESS", default_value = "0.0.0.0:3002")]
    bind_address: SocketAddr,
    /// Sets the benchmark frequency
    #[arg(long, value_name = "BENCHFREQUENCY", default_value = "10000")]
    bench_frequency: u32,

    /// Sets Ocean network
    #[arg(long, value_name = "NETWORK", default_value = "mainnet")]
    network: Network,
}

#[tokio::main]
async fn main() -> Result<()> {
    env_logger::init();
    let cli = Cli::parse();

    let yo = ain_cpp_imports::get_chain_id()?;
    println!("yo : {:?}", yo);
    let store = Arc::new(OceanStore::new(&cli.datadir)?);

    let client = Arc::new(
        Client::new(
            &cli.rpcaddress,
            Auth::UserPass(cli.user.clone(), cli.pass.clone()),
        )
        .await?,
    );

    let services = Arc::new(Services::new(store));

    let listener = tokio::net::TcpListener::bind(cli.bind_address).await?;
    let ocean_router =
        ain_ocean::ocean_router(&services, Arc::clone(&client), cli.network.to_string()).await?;
    tokio::spawn(async move { axum::serve(listener, ocean_router).await.unwrap() });

    let mut indexed_block = 0;
    let mut next_block_hash = None;
    let mut start_time = Instant::now();

    loop {
        let highest_block = services
            .block
            .by_height
            .get_highest()?
            .map_or(0, |b| b.height);
        let new_height = highest_block + 1;
        println!("Current indexed height {new_height}");
        let hash = if let Some(hash) = next_block_hash {
            hash
        } else {
            match client.get_block_hash(new_height).await {
                Ok(hash) => hash,
                Err(e) => {
                    println!("e : {:?}", e);
                    // Out of range, sleep for 10s
                    thread::sleep(Duration::from_millis(10000));
                    continue;
                }
            }
        };
        let block = match client.get_block(hash, 2).await {
            Err(e) => {
                println!("e : {:?}", e);
                // Error getting block, sleep for 30s
                thread::sleep(Duration::from_millis(30000));
                continue;
            }
            Ok(GetBlockResult::Full(block)) => block,
            Ok(_) => return Err("Error deserializing block".into()),
        };

        next_block_hash = block.nextblockhash;
        match index_block(&services, block) {
            Ok(_) => (),
            Err(e) => {
                return Err(e);
            }
        }

        indexed_block += 1;
        if indexed_block % cli.bench_frequency == 0 {
            let elapsed = start_time.elapsed();
            println!("Processed {} blocks in {:?}", cli.bench_frequency, elapsed);
            start_time = Instant::now();
        }
    }
}
