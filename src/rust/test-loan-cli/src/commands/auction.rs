use anyhow::{anyhow, Result};
use defi_rpc::json::vault::{BatchInfo, VaultData, VaultInfo};
use defi_rpc::Client;
use structopt::StructOpt;

#[derive(Debug, StructOpt)]
#[structopt(
    about = "Creates a vault, takes a loan of a specific token and liquidates vault. Requires a valid FixedIntervalPriceData."
)]
pub struct CreateAuctionCmd {
    /// Loan token.
    #[structopt(short, long)]
    token: String,

    #[structopt(long)]
    address: Option<String>,

    #[structopt(short, long, default_value = "1000")]
    amount: f64,
}

impl CreateAuctionCmd {
    pub fn run(&self, client: &Client) -> Result<()> {
        let token = &self.token;
        let amount = self.amount;
        let collateral_token = "DFI";
        let amount_to_deposit = &format!("{}@{}", amount, collateral_token);

        let fixed_interval_price_dfi = client.get_fixed_interval_price(collateral_token)?;
        if !fixed_interval_price_dfi.is_live {
            return Err(anyhow!(
                "Fixed interval price for DFI/USD is not live. Cannot create auction."
            ));
        }

        let fixed_interval_price = client.get_fixed_interval_price(token)?;
        if !fixed_interval_price.is_live {
            return Err(anyhow!(
                "Fixed interval price for {}/USD is not live. Cannot create auction.",
                token
            ));
        }

        let list_loan_schemes = client.list_loan_schemes()?;
        if list_loan_schemes.len() < 1 {
            return Err(anyhow!(
                "There should be at least 1 available loan schemes to create auction."
            ));
        }

        // Use address passed as args or create a new address and send it UTXO.
        let address = match self.address.to_owned() {
            Some(address) => address,
            None => {
                let address = client.get_new_address()?;
                let utxo_tx = client.utxo_to_account(&address, amount_to_deposit)?;
                client.await_n_confirmations(&utxo_tx, 1)?;
                address
            }
        };
        println!("Using address {}", address);

        // Create vault to take loan
        let loan_scheme = &list_loan_schemes[0];
        let vault_id = client.create_vault(&address, &loan_scheme.id)?;
        client.await_n_confirmations(&vault_id, 1)?;
        let vault = client.get_vault(&vault_id)?;
        println!("vault succesfully created : {:#?}", vault);

        // Deposit collateral to vault
        let deposit_tx = client.deposit_to_vault(&vault_id, &address, amount_to_deposit)?;
        client.await_n_confirmations(&deposit_tx, 1)?;
        println!("Succesfully deposited {} to vault", amount_to_deposit);

        // Calculate maximum amount of loan token vault can take
        let VaultData {
            collateral_value,
            collateral_amounts,
            ..
        } = match client.get_vault(&vault_id)? {
            VaultInfo::Active(active_vault) => Some(active_vault),
            _ => None,
        }
        .expect("Vault should be active");

        let fixed_interval_price = client.get_fixed_interval_price(token)?;
        println!("fixed_interval_price : {:#?}", fixed_interval_price);
        let min_loan_amount = collateral_value
            / (fixed_interval_price.next_price * (loan_scheme.mincolratio as f64))
            * 98f64; // Use 98 instead of to account for slippage
        println!("min_loan_amount : {}", min_loan_amount);
        let amount = &format!("{:.8}@{}", min_loan_amount, token);
        println!("amount : {}", amount);
        let take_loan_tx =
            client.take_loan(&vault_id, &format!("{:.8}@{}", min_loan_amount, token))?;
        client.await_n_confirmations(&take_loan_tx, 1)?;

        // Withdraw a progressively smaller amount from vault to trigger liquidation
        let (collateral_amount, _) = get_amount_symbol(&collateral_amounts[0]);
        let mut amount_to_withdraw = collateral_amount * 0.01;
        while amount_to_withdraw > 0.00000001 {
            println!("amount_to_withdraw : {}", amount_to_withdraw);
            match client.withdraw_from_vault(
                &vault_id,
                &address,
                &format!("{:.8}", amount_to_withdraw),
            ) {
                Ok(withdrawal_tx) => {
                    client.await_n_confirmations(&withdrawal_tx, 1)?;
                }
                Err(_) => {
                    amount_to_withdraw /= 2.;
                }
            }
        }
        println!("Sucessfully liquidated vault : {}", vault_id);
        Ok(())
    }
}

#[derive(Debug, StructOpt)]
#[structopt(about = "Automatically find the best auction to bid on.")]
pub struct AuctionBidCmd {
    /// Max value to bid.
    #[structopt(short, long, default_value = "3000")]
    max_value: f32,
}

pub fn get_amount_symbol(amount: &str) -> (f64, String) {
    let mut split = amount.split("@");
    (
        split
            .next()
            .expect("Amount should be formatted as <amount@symbol>")
            .parse::<f64>()
            .expect("Amount sould be parsed as float"),
        split
            .next()
            .expect("Amount should be formatted as <amount@symbol>")
            .to_owned(),
    )
}

#[derive(Debug, Clone)]
struct BatchValue {
    batch: BatchInfo,
    vault_id: String,
    collateral_amount: f64,
    collateral_value_usd: f64,
    amount_to_pay: f64,
    amount_to_pay_usd: f64,
    loan_symbol: String,
}

impl AuctionBidCmd {
    pub fn run(&self, client: &Client) -> Result<()> {
        let list_auctions = client.list_auctions()?;
        if list_auctions.len() < 1 {
            return Err(anyhow!(
                "There should be at least 1 available auction to bid on."
            ));
        }

        // Create vector of (batch, vault_id)
        let batches = list_auctions
            .into_iter()
            .flat_map(|auction| {
                auction
                    .batches
                    .iter()
                    .map(|b| (b.to_owned(), auction.vault_id.clone()))
                    .collect::<Vec<(BatchInfo, String)>>()
            })
            .collect::<Vec<(BatchInfo, String)>>();

        // Filter live price and create vector of (token_symbol, active_price)
        let list_fixed_interval_prices = client
            .list_fixed_interval_prices()?
            .iter()
            .filter(|price| price.is_live)
            .map(|price| {
                (
                    price
                        .price_feed_id
                        .split("/")
                        .next()
                        .expect("Price feed id should be formated as TOKEN/USD")
                        .to_owned(),
                    price.active_price,
                )
            })
            .collect::<Vec<(String, f64)>>();

        // Calculate total collateral value in USD and amount to pay for each batch
        let batch_values = batches
            .iter()
            .flat_map(|(batch, vault_id)| {
                let (collateral_amount, collateral_symbol) =
                    get_amount_symbol(&batch.collaterals[0]);
                let (loan_amount, loan_symbol) = get_amount_symbol(&batch.loan);

                let collateral_price_usd = match list_fixed_interval_prices
                    .iter()
                    .find(|(symbol, _)| symbol == &collateral_symbol)
                {
                    Some(price) => price.1,
                    None => return None,
                };
                let loan_price_usd = match list_fixed_interval_prices
                    .iter()
                    .find(|(symbol, _)| symbol == &loan_symbol)
                {
                    Some(price) => price.1,
                    None => return None,
                };

                let amount_to_pay = match &batch.highest_bid {
                    Some(bid) => {
                        let (bid_value, _) = get_amount_symbol(&bid.amount);
                        bid_value * 1.01
                    }
                    None => loan_amount * 1.05, // TODO replace by batch.liquidationPenalty
                };

                Some(BatchValue {
                    batch: batch.clone(),
                    vault_id: vault_id.into(),
                    collateral_amount,
                    collateral_value_usd: collateral_amount * collateral_price_usd,
                    amount_to_pay,
                    amount_to_pay_usd: amount_to_pay * loan_price_usd,
                    loan_symbol,
                })
            })
            .collect::<Vec<BatchValue>>();

        // Sorts auction batches by total_collateral_value_usd - price_to_pay
        let mut best_batch = batch_values.clone();
        best_batch.sort_by(|batch_a, batch_b| {
            (batch_b.collateral_value_usd - batch_b.amount_to_pay_usd)
                .partial_cmp(&(batch_a.collateral_value_usd - batch_a.amount_to_pay_usd))
                .unwrap()
        });

        let BatchValue {
            batch,
            amount_to_pay,
            amount_to_pay_usd,
            loan_symbol,
            collateral_amount,
            collateral_value_usd,
            vault_id,
        } = &best_batch[0];
        let address = client.get_new_address()?;
        let amount_to_pay = format!("{:.8}@{}", amount_to_pay, loan_symbol);
        let send_token_tx = client.send_tokens_to_address(&address, &amount_to_pay)?;
        client.await_n_confirmations(&send_token_tx, 1)?;
        println!(
            "Bidding {} for {} on vault {} auction index {}",
            amount_to_pay, collateral_amount, vault_id, batch.index
        );
        println!(
            "Potential gain of : {}",
            collateral_value_usd - amount_to_pay_usd
        );
        let auction_bid_tx = client.auction_bid(vault_id, batch.index, &address, &amount_to_pay)?;
        client.await_n_confirmations(&auction_bid_tx, 1)?;
        Ok(())
    }
}
