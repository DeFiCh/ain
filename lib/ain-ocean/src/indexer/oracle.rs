use std::collections::HashSet;

use dftx_rs::{common::CompactVec, oracles::*, Transaction};
use hyper::service::Service;

use super::BlockContext;
use crate::{
    indexer::{Index, Result},
    model::{
        OraclePriceAggregated, OraclePriceAggregatedAggregated,
        OraclePriceAggregatedAggregatedOracles, OraclePriceFeed, OracleTokenCurrency,
    },
    repository::RepositoryOps,
    SERVICES,
};

impl Index for AppointOracle {
    fn index(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        todo!()
    }
}

impl Index for RemoveOracle {
    fn index(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        todo!()
    }
}

impl Index for UpdateOracle {
    fn index(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        todo!()
    }
}

impl Index for SetOracleData {
    fn index(&self, ctx: &BlockContext, tx: Transaction, idx: usize) -> Result<()> {
        let feeds = map_price_feeds(vec![self], ctx, vec![tx])?;
        let mut pairs: HashSet<(String, String)> = HashSet::new();
        for feed in feeds {
            pairs.insert((feed.token.clone(), feed.currency.clone()));
            SERVICES.oracle_price_feed.by_id.put(&feed.id, &feed)?;
        }

        for (token, currency) in pairs.iter() {
            let aggregated_value = map_price_aggregated(ctx.clone(), token, currency);

            if let Some(value) = aggregated_value {
                let aggreated_id = (
                    value.token.clone(),
                    value.currency.clone(),
                    value.block.height.clone(),
                );
                let aggreated_key = (value.token.clone(), value.currency.clone());
                SERVICES
                    .oracle_price_aggregated
                    .by_id
                    .put(&aggreated_id, &value)?;
                SERVICES
                    .oracle_price_aggregated
                    .by_key
                    .put(&aggreated_key, &aggreated_id)?;
            }
        }
        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        todo!()
    }
}

fn map_price_aggregated(
    ctx: BlockContext,
    token: &str,
    currency: &str,
) -> Option<OraclePriceAggregated> {
    // Convert Result to Option
    let oracle_id = SERVICES
        .oracle_token_currency
        .by_key
        .list(Some((token.to_string(), currency.to_string())))
        .ok();
    let mut oracle_token_currencies = Vec::new();

    if let Some(mut oracle_iter) = oracle_id {
        loop {
            match oracle_iter.next() {
                Some(Ok((_, value))) => {
                    let oracle = SERVICES.oracle_token_currency.by_id.get(&value);
                    if let Ok(Some(oracle)) = oracle {
                        oracle_token_currencies.push(oracle);
                    }
                }

                Some(Err(e)) => {
                    println!("Error: {:?}", e);
                    break;
                }

                None => break,
            }
        }
    }

    let result = oracle_token_currencies;
    let mut aggregated = OraclePriceAggregatedAggregated {
        amount: "0".to_string(),
        weightage: 0,
        oracles: OraclePriceAggregatedAggregatedOracles {
            active: 0,
            total: 0,
        },
    };

    for oracle in result {
        if oracle.weightage == 0 {
            continue;
        }

        let key = (token.to_string(), currency.to_string(), oracle.oracle_id);
        let feed_id = SERVICES.oracle_price_feed.by_key.get(&key);

        let feeds = match feed_id {
            Ok(feed_ids) => feed_ids.map_or_else(
                || Ok(None),
                |id| {
                    SERVICES
                        .oracle_price_feed
                        .by_id
                        .get(&id)
                        .map(|data| Some(data))
                },
            ),
            Err(err) => Err(err), // Pass along the error if there was one
        };

        let oracle_price_feed: Option<OraclePriceFeed> = match feeds {
            Ok(Some(Some(feed))) => Some(feed),
            _ => None,
        };

        if oracle_price_feed.is_none() {
            continue;
        }

        if let Some(oracle_price_feed) = oracle_price_feed {
            if (oracle_price_feed.time - ctx.time) < 3600 {
                aggregated.oracles.active += 1;
                aggregated.weightage += oracle.weightage;

                let amount = oracle_price_feed.amount; // Assuming amount is already an i64

                let weighted_amount = amount * oracle.weightage as i64;

                if let Ok(current_amount) = aggregated.amount.parse::<i64>() {
                    aggregated.amount = (current_amount + weighted_amount).to_string();
                }
            }
        } else {
            continue;
        }
    }

    if aggregated.oracles.active == 0 {
        return None;
    }

    Some(OraclePriceAggregated {
        id: (token.to_string(), currency.to_string(), ctx.height),
        key: (token.to_string(), currency.to_string()),
        sort: format!(
            "{}{}",
            hex::encode(ctx.median_time.to_be_bytes()),
            hex::encode(ctx.height.to_be_bytes())
        ),
        token: token.to_string(),
        currency: currency.to_string(),
        aggregated,
        block: ctx,
    })
}

pub fn map_price_feeds(
    set_oracle_data: Vec<&SetOracleData>,
    ctx: &BlockContext,
    txns: Vec<Transaction>,
) -> Result<Vec<OraclePriceFeed>> {
    let mut result: Vec<OraclePriceFeed> = Vec::new();

    for (idx, tx) in txns.into_iter().enumerate() {
        // Use indexing to access elements in set_oracle_data
        let set_data = &set_oracle_data[idx];

        // Use as_ref() to get a reference to the inner vector
        let token_prices = set_data.token_prices.as_ref();

        // Perform additional processing and create OraclePriceFeed object
        for token_price in token_prices {
            for token_amount in token_price.prices.as_ref() {
                let oracle_price_feed = OraclePriceFeed {
                    id: (
                        token_price.token.clone(),
                        token_amount.currency.clone(),
                        set_data.oracle_id.to_string(),
                        tx.txid(),
                    ),

                    key: (
                        token_price.token.clone(),
                        token_amount.currency.clone(),
                        set_data.oracle_id.to_string(),
                    ),
                    sort: hex::encode(ctx.height.to_string() + &tx.txid().to_string()),
                    amount: token_amount.amount,
                    currency: token_amount.currency.clone(),
                    block: BlockContext {
                        hash: ctx.hash.clone(),
                        height: ctx.height,
                        median_time: ctx.median_time,
                        time: ctx.time,
                    },
                    oracle_id: set_data.oracle_id.to_string(),
                    time: set_data.timestamp as u64,
                    token: token_price.token.clone(),
                    txid: tx.txid(),
                };

                result.push(oracle_price_feed);
            }
        }
    }

    Ok(result)
}
