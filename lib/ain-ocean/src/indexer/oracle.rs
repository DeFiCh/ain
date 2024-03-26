use std::{collections::HashSet, str::FromStr, sync::Arc};

use ain_dftx::{common::CompactVec, oracles::*};
use bitcoin::Txid;
use rust_decimal::{
    prelude::{ToPrimitive, Zero},
    Decimal,
};

use crate::{
    error::NotFoundKind,
    indexer::{Context, Index, Result},
    model::{
        BlockContext, Oracle, OracleHistory, OracleIntervalSeconds, OraclePriceAggregated,
        OraclePriceAggregatedAggregated, OraclePriceAggregatedAggregatedOracles,
        OraclePriceAggregatedInterval, OraclePriceAggregatedIntervalAggregated,
        OraclePriceAggregatedIntervalAggregatedOracles, OraclePriceFeed, OracleTokenCurrency,
        SetOracleInterval,
    },
    repository::RepositoryOps,
    storage::SortOrder,
    Error, Services,
};

impl Index for AppointOracle {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
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

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let oracle_id = context.tx.txid;
        services.oracle.by_id.delete(&oracle_id)?;
        services.oracle_history.by_id.delete(&(
            oracle_id,
            context.block.height,
            context.tx.txid,
        ))?;
        for currency_pair in self.price_feeds.as_ref().iter() {
            let token_currency_id = (
                currency_pair.token.to_owned(),
                currency_pair.currency.to_owned(),
                oracle_id,
            );
            let token_currency_key = (
                currency_pair.token.to_owned(),
                currency_pair.currency.to_owned(),
            );
            services
                .oracle_token_currency
                .by_id
                .delete(&token_currency_id)?;
            services
                .oracle_token_currency
                .by_key
                .delete(&token_currency_key)?;
        }
        Ok(())
    }
}

impl Index for RemoveOracle {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
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
                        match services.oracle_token_currency.by_id.delete(&deletion_key) {
                            Ok(_) => {
                                // Successfully deleted
                            }
                            Err(err) => {
                                let error_message = format!("Error:remove oracle: {:?}", err);
                                eprintln!("{}", error_message);
                                return Err(Error::NotFound(NotFoundKind::Oracle));
                            }
                        }
                    }
                }
            }
            Err(err) => {
                let error_message = format!("Error:remove oracle: {:?}", err);
                eprintln!("{}", error_message);
                return Err(Error::NotFound(NotFoundKind::Oracle));
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
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
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
                        match services.oracle_token_currency.by_id.delete(&deletion_key) {
                            Ok(_) => {
                                // Successfully deleted
                            }
                            Err(err) => {
                                let error_message = format!("Error:update oracle: {:?}", err);
                                eprintln!("{}", error_message);
                                return Err(Error::NotFound(NotFoundKind::Oracle));
                            }
                        }
                    }
                }
            }
            Err(err) => {
                let error_message = format!("Error:update oracle: {:?}", err);
                eprintln!("{}", error_message);
                return Err(Error::NotFound(NotFoundKind::Oracle));
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
                        match services.oracle_token_currency.by_id.delete(&deletion_key) {
                            Ok(_) => {
                                // Successfully deleted
                            }
                            Err(err) => {
                                let error_message =
                                    format!("Error update oracle invalidate: {:?}", err);
                                eprintln!("{}", error_message);
                                return Err(Error::NotFound(NotFoundKind::Oracle));
                            }
                        }
                    }
                }
            }
            Err(err) => {
                let error_message = format!("Error update oracle invalidate: {:?}", err);
                eprintln!("{}", error_message);
                return Err(Error::NotFound(NotFoundKind::Oracle));
            }
        }

        Ok(())
    }
}

impl Index for SetOracleInterval {
    fn index(self, services: &Arc<Services>, context: &Context) -> Result<()> {
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
    fn index(self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let set_oracle_data = SetOracleData {
            oracle_id: self.oracle_id,
            timestamp: self.timestamp,
            token_prices: self.token_prices,
        };

        println!("the value inside set_oracle_data {:?}", set_oracle_data);
        let feeds = map_price_feeds(vec![&set_oracle_data], vec![context])?;
        println!("the value inside oracle_data {:?}", feeds);
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
    let key = (token.to_string(), currency.to_string());
    let oracle_token_currency = services
        .oracle_token_currency
        .by_key
        .list(Some(key), SortOrder::Descending)?
        .map(|item| {
            let (_, id) = item?;
            let b = services
                .oracle_token_currency
                .by_id
                .get(&id)?
                .ok_or("Missing block index")?;

            Ok(b)
        })
        .collect::<Result<Vec<_>>>()?;

    let result = oracle_token_currency;
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
                let amount = oracle_price_feed.amount;
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
                        set_data.oracle_id,
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
        .list(
            Some((token.to_owned(), currency.to_owned(), interval.clone())),
            SortOrder::Descending,
        )
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

pub fn get_previous_history(oracle_id: Txid, services: &Services) -> Result<OracleHistory> {
    let previous_history_result = services
        .oracle_history
        .by_key
        .list(Some(oracle_id), SortOrder::Descending)?
        .map(|item| {
            let (_, id) = item?;
            let b = services
                .oracle_history
                .by_id
                .get(&id)?
                .ok_or("Missing block index")?;

            Ok(b)
        })
        .collect::<Result<Vec<_>>>()?;

    Ok(previous_history_result[0].clone())
}

pub fn invalidate_oracle_interval(
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
        .list(
            Some((token.to_owned(), currency.to_owned(), interval.clone())),
            SortOrder::Descending,
        )
        .ok()
    {
        for result in previous_iter {
            match result {
                Ok((_, oracle_id)) => {
                    if let Some(inner_values) = services
                        .oracle_price_aggregated_interval
                        .by_id
                        .list(Some(oracle_id), SortOrder::Descending)
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
                                        let lastprice = oracle_price_aggreated.aggregated.clone();
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
) {
    let cloned_interval = interval.clone();
    if previous_data.clone().is_some()
        || (block.median_time - previous_data.clone().unwrap().block.median_time.clone())
            > cloned_interval as i64
    {
        let oracle_price_aggregated_interval = OraclePriceAggregatedInterval {
            id: ((
                token.to_owned(),
                currency.to_owned(),
                interval.clone(),
                block.height,
            )),
            key: ((token.to_owned(), currency.to_owned(), interval)),
            sort: aggregated.sort.to_owned(),
            token: token.to_owned(),
            currency: currency.to_owned(),
            aggregated: previous_data.as_ref().unwrap().aggregated.clone(),
            block: block,
        };
        let err = services.oracle_price_aggregated_interval.by_id.put(
            &oracle_price_aggregated_interval.id,
            &oracle_price_aggregated_interval,
        );
        let err = services.oracle_price_aggregated_interval.by_key.put(
            &oracle_price_aggregated_interval.key,
            &oracle_price_aggregated_interval.id,
        );
    } else {
        // Add logic for the case when the condition is false
        let lastprice = previous_data.as_ref().unwrap().aggregated.clone();
        let count = lastprice.count + 1;
        let aggregatedInterval = OraclePriceAggregatedInterval {
            id: previous_data.as_ref().unwrap().id.clone(),
            key: previous_data.as_ref().unwrap().key.clone(),
            sort: previous_data.as_ref().unwrap().sort.clone(),
            token: previous_data.as_ref().unwrap().token.clone(),
            currency: previous_data.as_ref().unwrap().currency.clone(),
            aggregated: OraclePriceAggregatedIntervalAggregated {
                amount: forward_aggregate_value(
                    lastprice.amount.as_str(),
                    aggregated.aggregated.amount.as_str(),
                    count,
                )
                .to_string(),
                weightage: forward_aggregate_number(
                    lastprice.weightage,
                    aggregated.aggregated.weightage,
                    count,
                ),
                count: count,
                oracles: OraclePriceAggregatedIntervalAggregatedOracles {
                    active: forward_aggregate_number(
                        lastprice.oracles.active,
                        aggregated.aggregated.oracles.active,
                        lastprice.count,
                    ) as i32,
                    total: forward_aggregate_number(
                        lastprice.oracles.total,
                        aggregated.aggregated.oracles.total,
                        lastprice.count,
                    ),
                },
            },
            block: previous_data.as_ref().unwrap().block.clone(),
        };
    }
}

fn forward_aggregate_number(last_value: i32, new_value: i32, count: i32) -> i32 {
    let count_decimal = Decimal::from(count);
    let last_value_decimal = Decimal::from(last_value);
    let new_value_decimal = Decimal::from(new_value);

    let result = (last_value_decimal * count_decimal + new_value_decimal)
        / (count_decimal + Decimal::from(1));

    result.to_i32().unwrap_or_else(|| {
        eprintln!("Result is too large to fit into i32, returning 0");
        i32::MAX
    })
}

fn forward_aggregate_value(last_value: &str, new_value: &str, count: i32) -> Decimal {
    let last_decimal = Decimal::from_str(last_value).unwrap();
    let new_decimal = Decimal::from_str(new_value).unwrap();
    let count_decimal = Decimal::from(count);

    let result = last_decimal * count_decimal + new_decimal;
    result / (count_decimal + Decimal::from(1))
}

fn backward_aggregate_value(last_value: &str, new_value: &str, count: u32) -> Decimal {
    let last_value_decimal = Decimal::from_str(last_value).unwrap_or_else(|_| Decimal::zero());
    let new_value_decimal = Decimal::from_str(new_value).unwrap_or_else(|_| Decimal::zero());
    let count_decimal = Decimal::from(count);

    (last_value_decimal * count_decimal.clone() - new_value_decimal)
        / (count_decimal - Decimal::from(1))
}

fn backward_aggregate_number(last_value: i32, new_value: i32, count: u32) -> i32 {
    let last_value_decimal =
        Decimal::from_str(&last_value.to_string()).unwrap_or_else(|_| Decimal::zero());
    let new_value_decimal =
        Decimal::from_str(&new_value.to_string()).unwrap_or_else(|_| Decimal::zero());
    let count_decimal = Decimal::from(count);

    let result = (last_value_decimal * count_decimal.clone() - new_value_decimal)
        / (count_decimal - Decimal::from(1));

    result.to_i32().unwrap_or_else(|| {
        eprintln!("Result is too large to fit into i32, returning 0");
        0
    })
}
