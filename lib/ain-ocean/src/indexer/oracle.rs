use std::{collections::HashSet, sync::Arc};

use ain_dftx::oracles::*;
use dftx_rs::{common::CompactVec, oracles::*};
use num_bigint::BigUint;
use num_traits::{FromPrimitive, One, ToPrimitive, Zero};

use crate::{
    indexer::{Context, Index, Result},
    model::{
        BlockContext, Oracle, OracleHistory, OracleIntervalSeconds, OraclePriceAggregated,
        OraclePriceAggregatedAggregated, OraclePriceAggregatedAggregatedOracles,
        OraclePriceAggregatedInterval, OraclePriceAggregatedIntervalAggregated,
        OraclePriceAggregatedIntervalAggregatedOracles, OraclePriceFeed, OracleTokenCurrency,
        SetOracleInterval,
    },
    repository::RepositoryOps,
    Services,
};

impl Index for AppointOracle {
    fn index(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        let oracle_id = ctx.tx.txid;
        let oracle = Oracle {
            id: oracle_id,
            owner_address: self.script.to_hex_string(),
            weightage: self.weightage,
            price_feeds: vec![],
            block: ctx.block.clone(),
        };
        services.oracle.by_id.put(&oracle.id, &oracle)?;
        let oracle_history = OracleHistory {
            id: (ctx.tx.txid, ctx.block.height, oracle_id),
            oracle_id: ctx.tx.txid,
            sort: format!(
                "{}{}",
                hex::encode(ctx.block.height.to_be_bytes()),
                ctx.tx.txid
            ),
            owner_address: self.script.to_hex_string(),
            weightage: self.weightage,
            price_feeds: vec![],
            block: ctx.block.clone(),
        };

        services
            .oracle_history
            .by_id
            .put(&oracle_history.id, &oracle_history)?;

        let prices_feeds = self.price_feeds.as_ref();

        for token_currency in prices_feeds {
            let oracle_token_currency = OracleTokenCurrency {
                id: (
                    token_currency.token.to_owned(),
                    token_currency.currency.to_owned(),
                    oracle_id,
                ),
                key: (
                    token_currency.token.to_owned(),
                    token_currency.currency.to_owned(),
                ),
                token: token_currency.token.to_owned(),
                currency: token_currency.currency.to_owned(),
                oracle_id: oracle_id,
                weightage: self.weightage,
                block: ctx.block.clone(),
            };

            services
                .oracle_token_currency
                .by_id
                .put(&oracle_token_currency.id, &oracle_token_currency)?;
        }

        Ok(())
    }

    fn invalidate(&self, _services: &Arc<Services>, _context: &Context) -> Result<()> {
        todo!()
    }
}

impl Index for RemoveOracle {
    fn index(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        let oracle_id = ctx.tx.txid;
        //delete for oracle
        services.oracle.by_id.delete(&oracle_id)?;
        match services.oracle.by_id.get(&oracle_id) {
            Ok(previous_oracle_result) => {
                if let Some(previous_oracle) = previous_oracle_result {
                    for price_feed_item in &previous_oracle.price_feeds {
                        let deletion_key = (
                            price_feed_item.token.to_owned(),
                            price_feed_item.currency.to_owned(),
                            oracle_id,
                        );
                        if let Err(err) = services.oracle_token_currency.by_id.delete(&deletion_key)
                        {
                            eprintln!("Error removing oracle: {:?}", err);
                        }
                    }
                }
            }
            Err(err) => {
                eprintln!("Error getting previous oracle: {:?}", err);
            }
        }
        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let oracle_id = context.tx.txid;
        match services.oracle.by_id.get(&oracle_id) {
            Ok(previous_oracle_result) => {
                if let Some(previous_oracle) = previous_oracle_result {
                    let oracle = Oracle {
                        id: previous_oracle.id,
                        owner_address: previous_oracle.owner_address,
                        weightage: previous_oracle.weightage,
                        price_feeds: vec![],
                        block: previous_oracle.block,
                    };
                    services.oracle.by_id.put(&oracle.id, &oracle)?;

                    for prev_token_currency in previous_oracle.price_feeds {
                        let oracle_token_currency = OracleTokenCurrency {
                            id: (
                                prev_token_currency.token.clone(),
                                prev_token_currency.currency.clone(),
                                oracle.id,
                            ),
                            key: (
                                prev_token_currency.token.clone(),
                                prev_token_currency.currency.clone(),
                            ),
                            token: prev_token_currency.token,
                            currency: prev_token_currency.currency.to_owned(),
                            oracle_id: oracle_id,
                            weightage: oracle.weightage,
                            block: oracle.block.clone(),
                        };

                        services
                            .oracle_token_currency
                            .by_id
                            .put(&oracle_token_currency.id, &oracle_token_currency)?;
                    }
                } else {
                    eprintln!("Error saving previous oracle data",);
                }
            }
            Err(err) => {
                eprintln!("Error getting previous oracle: {:?}", err);
            }
        }

        Ok(())
    }
}

impl Index for UpdateOracle {
    fn index(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        let oracle_id = ctx.tx.txid;
        let oracle = Oracle {
            id: oracle_id,
            owner_address: self.script.to_hex_string(),
            weightage: self.weightage,
            price_feeds: vec![],
            block: ctx.block.clone(),
        };
        //store value in oracle
        services.oracle.by_id.put(&oracle.id, &oracle)?;

        match services.oracle.by_id.get(&oracle.id) {
            Ok(previous_oracle_result) => {
                if let Some(previous_oracle) = previous_oracle_result {
                    for price_feed_item in &previous_oracle.price_feeds {
                        // Assuming `oracle_id` is a field in `data` that you want to use for deletion
                        let deletion_key = (
                            price_feed_item.token.clone(),
                            price_feed_item.currency.clone(),
                            oracle_id,
                        );
                        if let Err(err) = services.oracle_token_currency.by_id.delete(&deletion_key)
                        {
                            // Handle the error, if necessary
                            eprintln!("Error deleting data: {:?}", err);
                        }
                    }
                }
            }
            Err(err) => {
                // Handle the error from the Result
                eprintln!("Error getting previous oracle: {:?}", err);
            }
        }
        let prices_feeds = self.price_feeds.as_ref();
        //saving value in oracle_token_currency
        for token_currency in prices_feeds {
            let oracle_token_currency = OracleTokenCurrency {
                id: (
                    token_currency.token.clone(),
                    token_currency.currency.clone(),
                    oracle_id,
                ),
                key: (
                    token_currency.token.clone(),
                    token_currency.currency.clone(),
                ),
                token: token_currency.token.clone(),
                currency: token_currency.currency.clone(),
                oracle_id: oracle_id.clone(),
                weightage: self.weightage,
                block: ctx.block.clone(),
            };

            services
                .oracle_token_currency
                .by_id
                .put(&oracle_token_currency.id, &oracle_token_currency)?;
        }

        let oracle_history = OracleHistory {
            id: (ctx.tx.txid, ctx.block.height, oracle_id),
            oracle_id: ctx.tx.txid,
            sort: format!(
                "{}{}",
                hex::encode(ctx.block.height.to_be_bytes()),
                ctx.tx.txid
            ),
            owner_address: self.script.to_hex_string(),
            weightage: self.weightage,
            price_feeds: vec![],
            block: ctx.block.clone(),
        };
        //saving value in oracle_history
        services
            .oracle_history
            .by_id
            .put(&oracle_history.id, &oracle_history)?;

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let oracle_id = context.tx.txid;
        services.oracle_history.by_id.delete(&(
            oracle_id,
            context.block.height,
            context.tx.txid,
        ))?;
        match services.oracle.by_id.get(&oracle_id) {
            Ok(previous_oracle_result) => {
                if let Some(previous_oracle) = previous_oracle_result {
                    for price_feed_item in &previous_oracle.price_feeds {
                        let deletion_key = (
                            price_feed_item.token.clone(),
                            price_feed_item.currency.clone(),
                            oracle_id,
                        );
                        if let Err(err) = services.oracle_token_currency.by_id.delete(&deletion_key)
                        {
                            eprintln!("Error deleting data: {:?}", err);
                        }
                    }
                }
            }
            Err(err) => {
                eprintln!("Error getting previous oracle: {:?}", err);
            }
        }

        Ok(())
    }
}

impl Index for SetOracleInterval {
    fn index(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let set_oracle_data = SetOracleData {
            oracle_id: self.oracle_id,
            timestamp: self.timestamp,
            token_prices: CompactVec::from(Vec::new()),
        };

        let feeds = map_price_feeds(vec![&set_oracle_data], vec![context])?;
        let mut pairs: HashSet<(String, String)> = HashSet::new();
        for feed in feeds {
            pairs.insert((feed.token.clone(), feed.currency.clone()));
        }
        let intervals: Vec<OracleIntervalSeconds> = vec![
            OracleIntervalSeconds::FifteenMinutes,
            OracleIntervalSeconds::OneHour,
            OracleIntervalSeconds::OneDay,
        ];
        for (token, currency) in pairs.iter() {
            let aggregated = services.oracle_price_aggregated.by_id.get(&(
                token.to_owned(),
                currency.to_owned(),
                context.block.height,
            ))?;

            for interval in intervals.clone() {
                // Call the equivalent of indexIntervalMapper in Rust
                index_interval_mapper(
                    services,
                    &context.block,
                    token,
                    currency,
                    aggregated.as_ref().unwrap(),
                    interval,
                );
            }
        }

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        todo!() // follow invalidate_oracle_interval method
    }
}

impl Index for SetOracleData {
    fn index(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let set_oracle_data = SetOracleData {
            oracle_id: self.oracle_id,
            timestamp: self.timestamp,
            token_prices: CompactVec::from(Vec::new()),
        };

        let feeds = map_price_feeds(vec![&set_oracle_data], vec![context])?;
        let mut pairs: HashSet<(String, String)> = HashSet::new();
        for feed in feeds {
            pairs.insert((feed.token.clone(), feed.currency.clone()));
            services.oracle_price_feed.by_id.put(&feed.id, &feed)?;
        }

        for (token, currency) in pairs.iter() {
            let aggregated_value = map_price_aggregated(services, context, token, currency);

            if let Ok(Some(value)) = aggregated_value {
                let aggreated_id = (
                    value.token.clone(),
                    value.currency.clone(),
                    value.block.height.clone(),
                );
                let aggreated_key = (value.token.clone(), value.currency.clone());
                services
                    .oracle_price_aggregated
                    .by_id
                    .put(&aggreated_id, &value)?;
                services
                    .oracle_price_aggregated
                    .by_key
                    .put(&aggreated_key, &aggreated_id)?;
            }
        }
        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let set_oracle_data = SetOracleData {
            oracle_id: self.oracle_id,
            timestamp: self.timestamp,
            token_prices: CompactVec::from(Vec::new()),
        };
        let feeds = map_price_feeds(vec![&set_oracle_data], vec![context])?;
        let mut pairs: HashSet<(String, String)> = HashSet::new();
        for feed in feeds {
            pairs.insert((feed.token.clone(), feed.currency.clone()));
            services.oracle_price_feed.by_id.delete(&feed.id)?;
        }
        for (token, currency) in pairs.iter() {
            let aggreated_id = (token.to_owned(), currency.to_owned(), context.block.height);
            services
                .oracle_price_aggregated
                .by_id
                .delete(&aggreated_id)?;
        }
        Ok(())
    }
}

pub fn map_price_aggregated(
    services: &Arc<Services>,
    ctx: &Context,
    token: &str,
    currency: &str,
) -> Result<Option<OraclePriceAggregated>> {
    // Convert Result to Option
    let oracle_id = services
        .oracle_token_currency
        .by_key
        .list(Some((token.to_string(), currency.to_string())))
        .ok();
    let mut oracle_token_currencies = Vec::new();

    if let Some(mut oracle_iter) = oracle_id {
        loop {
            match oracle_iter.next() {
                Some(Ok((_, value))) => {
                    let oracle = services.oracle_token_currency.by_id.get(&value);
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
        let feed_id = services.oracle_price_feed.by_key.get(&key);

        let feeds = match feed_id {
            Ok(feed_ids) => feed_ids.map_or_else(
                || Ok(None),
                |id| {
                    services
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
            if (oracle_price_feed.time - ctx.block.time as i32) < 3600 {
                aggregated.oracles.active += 1;
                aggregated.weightage += oracle.weightage as i32;

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
        return Ok(None); // Replace with an appropriate error variant
    };

    Ok(Some(OraclePriceAggregated {
        id: (token.to_string(), currency.to_string(), ctx.block.height),
        key: (token.to_string(), currency.to_string()),
        sort: format!(
            "{}{}",
            hex::encode(ctx.block.median_time.to_be_bytes()),
            hex::encode(ctx.block.height.to_be_bytes())
        ),
        token: token.to_string(),
        currency: currency.to_string(),
        aggregated,
        block: ctx.block.clone(),
    }))
}

pub fn map_price_feeds(
    set_oracle_data: Vec<&SetOracleData>,
    context: Vec<&Context>,
) -> Result<Vec<OraclePriceFeed>> {
    let mut result: Vec<OraclePriceFeed> = Vec::new();

    for (idx, ctx) in context.into_iter().enumerate() {
        // Use indexing to access elements in set_oracle_data
        let set_data = set_oracle_data[idx];

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
                        ctx.tx.txid,
                    ),

                    key: (
                        token_price.token.clone(),
                        token_amount.currency.clone(),
                        set_data.oracle_id,
                    ),
                    sort: hex::encode(ctx.block.height.to_string() + &ctx.tx.txid.to_string()),
                    amount: token_amount.amount,
                    currency: token_amount.currency.clone(),
                    block: ctx.block.clone(),
                    oracle_id: set_data.oracle_id,
                    time: set_data.timestamp as i32,
                    token: token_price.token.clone(),
                    txid: ctx.tx.txid,
                };

                result.push(oracle_price_feed);
            }
        }
    }

    Ok(result)
}

pub fn index_interval_mapper(
    services: &Arc<Services>,
    block: &BlockContext,
    token: &str,
    currency: &str,
    aggregated: &OraclePriceAggregated,
    interval: OracleIntervalSeconds,
) {
    if let Some(previous_iter) = services
        .oracle_price_aggregated_interval
        .by_key
        .list(Some((
            token.to_owned(),
            currency.to_owned(),
            interval.clone(),
        )))
        .ok()
    {
        for result in previous_iter {
            match result {
                Ok((_, oracle_id)) => {
                    if let Some(inner_values) = services
                        .oracle_price_aggregated_interval
                        .by_id
                        .get(&oracle_id)
                        .ok()
                    {
                        process_inner_values(
                            &services,
                            inner_values,
                            block.clone(),
                            token,
                            currency,
                            aggregated,
                            interval.clone(),
                        );
                    } else {
                        // Handle the case when inner_values is None or Err
                    }
                }
                Err(db_error) => {
                    // Handle the error if needed
                    println!("Error in outer iterator: {:?}", db_error);
                }
            }
        }
    }
}

fn invalidate_oracle_interval(
    services: &Arc<Services>,
    block: &BlockContext,
    token: &str,
    currency: &str,
    aggregated: &OraclePriceAggregated,
    interval: OracleIntervalSeconds,
) {
    if let Some(previous_iter) = services
        .oracle_price_aggregated_interval
        .by_key
        .list(Some((
            token.to_owned(),
            currency.to_owned(),
            interval.clone(),
        )))
        .ok()
    {
        for result in previous_iter {
            match result {
                Ok((_, oracle_id)) => {
                    if let Some(inner_values) = services
                        .oracle_price_aggregated_interval
                        .by_id
                        .list(Some(oracle_id))
                        .ok()
                    {
                        // Process inner_values when it is Some
                        for result in inner_values {
                            match result {
                                Ok(((_, _, _, _), oracle_price_aggreated)) => {
                                    if oracle_price_aggreated.aggregated.count != 1 {
                                        let err = services
                                            .oracle_price_aggregated_interval
                                            .by_id
                                            .delete(&oracle_price_aggreated.id);
                                    } else {
                                        let lastprice = oracle_price_aggreated.aggregated;
                                        let count = lastprice.count - 1;
                                        let aggregatedInterval = OraclePriceAggregatedInterval {
                                            id: oracle_price_aggreated.id.clone(),
                                            key: oracle_price_aggreated.key.clone(),
                                            sort: oracle_price_aggreated.sort.clone(),
                                            token: oracle_price_aggreated.token.clone(),
                                            currency: oracle_price_aggreated.currency.clone(),
                                            aggregated: OraclePriceAggregatedIntervalAggregated {
                                                amount: backward_aggregate_value(
                                                    lastprice.amount.as_str(),
                                                    &aggregated.aggregated.amount.to_string(),
                                                    count as u32,
                                                )
                                                .to_string(),
                                                weightage: backward_aggregate_number(
                                                    lastprice.weightage,
                                                    aggregated.aggregated.weightage,
                                                    count as u32,
                                                ),
                                                count: count,
                                                oracles:
                                                    OraclePriceAggregatedIntervalAggregatedOracles {
                                                        active: backward_aggregate_number(
                                                            lastprice.oracles.active,
                                                            aggregated.aggregated.oracles.active,
                                                            lastprice.count as u32,
                                                        )
                                                            as i32,
                                                        total: backward_aggregate_number(
                                                            lastprice.oracles.total,
                                                            aggregated.aggregated.oracles.total,
                                                            lastprice.count as u32,
                                                        ),
                                                    },
                                            },
                                            block: oracle_price_aggreated.block.clone(),
                                        };
                                    }
                                }
                                Err(db_error) => {}
                            }
                        }
                    }
                }
                Err(db_error) => {
                    // Handle the error if needed
                    println!("Error in outer iterator: {:?}", db_error);
                }
            }
        }
    }
}

fn process_inner_values(
    services: &Arc<Services>,
    previous_data: Option<OraclePriceAggregatedInterval>,
    block: BlockContext,
    token: &str,
    currency: &str,
    aggregated: &OraclePriceAggregated,
    interval: OracleIntervalSeconds,
) -> Result<()> {
    let cloned_interval = interval.clone();

    if let Some(previous_data) = previous_data {
        // Clone previous data for modification
        let mut new_data = previous_data.clone();

        if (block.median_time - previous_data.block.median_time) > cloned_interval as i64 {
            new_data.id = ((
                token.to_owned(),
                currency.to_owned(),
                interval.clone(),
                block.height,
            ));
            new_data.key = ((token.to_owned(), currency.to_owned(), interval));
            new_data.sort = aggregated.sort.to_owned();
            new_data.token = token.to_owned();
            new_data.currency = currency.to_owned();
            new_data.aggregated = previous_data.aggregated;

            // Handle errors appropriately based on your needs
            let _ = services
                .oracle_price_aggregated_interval
                .by_id
                .put(&new_data.id, &new_data);
            let _ = services
                .oracle_price_aggregated_interval
                .by_key
                .put(&new_data.key, &new_data.id);
        } else {
            // Add logic for the case when the condition is false
            let last_price = previous_data.aggregated;
            let count = last_price.count + 1;

            new_data.aggregated = OraclePriceAggregatedIntervalAggregated {
                amount: forward_aggregate_value(
                    last_price.amount.as_str(),
                    &aggregated.aggregated.amount.to_string(),
                    count as u32,
                )
                .to_string(),
                weightage: forward_aggregate_number(
                    last_price.weightage,
                    aggregated.aggregated.weightage,
                    count,
                ),
                count,
                oracles: OraclePriceAggregatedIntervalAggregatedOracles {
                    active: forward_aggregate_number(
                        last_price.oracles.active,
                        aggregated.aggregated.oracles.active,
                        last_price.count,
                    ) as i32,
                    total: forward_aggregate_number(
                        last_price.oracles.total,
                        aggregated.aggregated.oracles.total,
                        last_price.count,
                    ),
                },
            };
        }
    } else {
        eprintln!("OraclePriceAggregatedInterval returning empty data");
    }

    Ok(())
}

fn forward_aggregate_number(last_value: i32, new_value: i32, count: i32) -> i32 {
    let count_bigint: BigUint = BigUint::from(count as u32);
    let last_value_bigint: BigUint = BigUint::from(last_value as u32);
    let new_value_bigint: BigUint = BigUint::from(new_value as u32);
    let result = (last_value_bigint * &count_bigint + new_value_bigint)
        / (count_bigint + BigUint::from(1u32));

    // Attempt to convert the result to u32 and then to i32. Handle overflow appropriately.
    result
        .to_i32()
        .and_then(|v| BigUint::from_i32(v))
        .and_then(|v| v.to_i32())
        .unwrap_or_else(|| {
            eprintln!("Overflow occurred. Returning i32::MAX");
            i32::MAX
        })
}
pub fn forward_aggregate_value(last_value: &str, new_value: &str, count: u32) -> BigUint {
    let last_value = last_value
        .parse::<BigUint>()
        .unwrap_or_else(|_| BigUint::zero());
    let new_value = new_value
        .parse::<BigUint>()
        .unwrap_or_else(|_| BigUint::zero());
    let count = BigUint::from(count);

    (last_value * count.clone() + new_value) / (count + BigUint::one())
}

fn backward_aggregate_value(last_value: &str, new_value: &str, count: u32) -> BigUint {
    let last_value = last_value
        .parse::<BigUint>()
        .unwrap_or_else(|_| BigUint::zero());
    let new_value = new_value
        .parse::<BigUint>()
        .unwrap_or_else(|_| BigUint::zero());
    let count = BigUint::from(count);

    (last_value * count.clone() - new_value) / (count - BigUint::one())
}

fn backward_aggregate_number(last_value: i32, new_value: i32, count: u32) -> i32 {
    let last_value = BigUint::from_i32(last_value);
    let new_value = BigUint::from_i32(new_value);
    let count = BigUint::from(count);

    let result =
        (last_value.unwrap() * &count.clone() - &new_value.unwrap()) / (count - BigUint::one());

    result.to_i32().unwrap_or_else(|| {
        eprintln!("Result is too large to fit into i32, returning 0");
        0
    })
}
