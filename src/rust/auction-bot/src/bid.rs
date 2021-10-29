use crate::error::BidError;
use anyhow::Result;
use defi_rpc::json::vault::BatchInfo;
use defi_rpc::Client;

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
    collateral_symbol: String,
}

// Filter valid price and create vector of (token_symbol, active_price)
fn list_live_active_prices(client: &Client) -> Result<Vec<(String, f64)>> {
    Ok(client
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
        .collect())
}

pub fn bid(client: &Client, address: &str) -> core::result::Result<(), BidError> {
    let list_auctions = client.list_auctions()?;
    if list_auctions.len() < 1 {
        return Err(BidError::NoBids);
    }

    // Create vector of (batch, vault_id). Filters out batches on which used addess is already highest bidder.
    let batches: Vec<(BatchInfo, String)> = list_auctions
        .into_iter()
        .flat_map(|auction| {
            auction
                .batches
                .iter()
                .flat_map(|batch| match &batch.highest_bid {
                    Some(bid) if bid.owner == address => {
                        println!("Already highest bidder on batch {:#?}", bid);
                        None
                    }
                    _ => Some((batch.to_owned(), auction.vault_id.clone())),
                })
                .collect::<Vec<(BatchInfo, String)>>()
        })
        .collect();

    if batches.len() < 1 {
        return Err(BidError::NoBatches);
    }

    let list_valid_active_prices = list_live_active_prices(client)?;

    // Calculate total collateral value in USD and amount to pay for each batch
    let batch_values = batches
        .iter()
        .flat_map(|(batch, vault_id)| {
            let (collateral_amount, collateral_symbol) = get_amount_symbol(&batch.collaterals[0]);
            let (loan_amount, loan_symbol) = get_amount_symbol(&batch.loan);

            let collateral_price_usd = match list_valid_active_prices
                .iter()
                .find(|(symbol, _)| symbol == &collateral_symbol)
            {
                Some(price) => price.1,
                None => return None,
            };
            let loan_price_usd = match list_valid_active_prices
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
                None => loan_amount * 1.05, // TODO replace by vault.liquidationPenalty
            };

            Some(BatchValue {
                batch: batch.clone(),
                vault_id: vault_id.into(),
                collateral_amount,
                collateral_value_usd: collateral_amount * collateral_price_usd,
                amount_to_pay,
                amount_to_pay_usd: amount_to_pay * loan_price_usd,
                loan_symbol,
                collateral_symbol,
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
        collateral_symbol,
        collateral_amount,
        collateral_value_usd,
        vault_id,
    } = match &best_batch.get(0) {
        Some(batch) => batch,
        None => return Err(BidError::NoBatches),
    };

    let amount_to_pay = format!("{:.8}@{}", amount_to_pay, loan_symbol);
    let send_token_tx = client.send_tokens_to_address(&address, &amount_to_pay)?;
    client.await_n_confirmations(&send_token_tx, 1)?;

    let auction_bid_tx = client.auction_bid(vault_id, batch.index, &address, &amount_to_pay)?;
    client.await_n_confirmations(&auction_bid_tx, 1)?;
    println!(
        "Bid {} for {:.8}@{} on vault {} auction index {}",
        amount_to_pay, collateral_amount, collateral_symbol, vault_id, batch.index
    );
    println!(
        "Potential gain of : {}USD",
        collateral_value_usd - amount_to_pay_usd
    );

    Ok(())
}
