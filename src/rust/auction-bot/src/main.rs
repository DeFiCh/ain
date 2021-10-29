mod bid;
pub mod error;

use bid::*;
use chain_params::Chain;
use defi_rpc::Client;
use error::BidError;
use structopt::StructOpt;

#[derive(StructOpt, Debug)]
#[structopt(name = "auction-bot")]
pub struct Opt {
    /// RPC user
    #[structopt(short, long)]
    user: Option<String>,

    /// RPC password
    #[structopt(short, long)]
    password: Option<String>,

    /// Address with DFI collaterals
    #[structopt(long)]
    address: Option<String>,

    /// Vault on which to bid. Will auto-select the one with most amount of collateral to win if not passed.
    #[structopt(long)]
    vault_id: Option<String>,

    // TODO Max collateral amount to spend.
    #[structopt(long, default_value = "100000")]
    max_amount: f32,

    /// Specific chain to run.
    #[structopt(
        short,
        long,
        default_value = "regtest",
        possible_values = &["mainnet", "testnet", "regtest", "devnet"]
      )]
    pub chain: Chain,
}

use std::sync::Arc;

fn main() -> Result<(), BidError> {
    println!("Defichain auction bot");
    let opt = Opt::from_args();
    let user = opt.user.unwrap_or(String::from("cake"));
    let password = opt.password.unwrap_or(String::from("cake"));
    let chain_params = opt.chain.get_params();
    let client = Arc::new(Client::new(
        &format!("http://localhost:{}", chain_params.rpc_port),
        user,
        password,
        String::from(chain_params.network),
    )?);

    let address = Arc::new(
        opt.address
            .unwrap_or(String::from(client.get_new_address()?)),
    );

    let spawn_address = address.clone();
    let spawn_client = client.clone();
    std::thread::spawn(move || {
        let mut buffer = String::new();
        loop {
            std::io::stdin().read_line(&mut buffer).unwrap();
            match buffer.as_str() {
                "history\n" | "h\n" => {
                    println!("-----------------------------------------------");
                    println!("Fetching history for address {}", spawn_address);
                    println!("-----------------------------------------------");
                    match spawn_client.list_auction_history(Some(&spawn_address)) {
                        Ok(history) => println!("history : {:#?}", history),
                        Err(e) => eprintln!("Error getting history : {}", e),
                    }
                }
                _ => (),
            }
            buffer.clear()
        }
    });

    loop {
        match bid(&client, &address) {
            Ok(_) => (),
            Err(BidError::NoBids) => {
                println!("No available bids, let me sleep for a bit...");
            }
            Err(BidError::NoBatches) => {
                println!("No available batches, let me sleep for a bit...");
            }
            Err(BidError::Anyhow(e)) => {
                eprintln!("{}", e);
            }
        };

        std::thread::sleep(std::time::Duration::from_secs(60));
    }
    Ok(())
}
