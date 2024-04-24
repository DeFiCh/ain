use std::{collections::HashSet, str::FromStr, sync::Arc, vec};

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
        PriceFeedsItem, PriceTicker,
    },
    repository::RepositoryOps,
    storage::SortOrder,
    Error, Services,
};

impl Index for AppointOracle {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        let oracle_id = ctx.tx.txid;

        let price_feeds_items: Vec<PriceFeedsItem> = self
            .price_feeds
            .iter()
            .map(|pair| PriceFeedsItem {
                token: pair.token.clone(),
                currency: pair.currency.clone(),
            })
            .collect();

        let oracle = Oracle {
            id: oracle_id,
            owner_address: self.script.to_hex_string(),
            weightage: self.weightage,
            price_feeds: price_feeds_items.clone(),
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
            price_feeds: price_feeds_items,
            block: ctx.block.clone(),
        };
        services
            .oracle_history
            .by_id
            .put(&oracle_history.id, &oracle_history)?;
        services
            .oracle_history
            .by_key
            .put(&oracle_history.oracle_id, &oracle_history.id)?;
        let mut indexed_tokens = HashSet::new();
        let prices_feeds = self.price_feeds.as_ref();
        for token_currency in prices_feeds {
            let id = (
                token_currency.token.clone(),
                token_currency.currency.clone(),
                oracle_id,
            );
            if !indexed_tokens.insert(id.clone()) {
                continue;
            }

            let oracle_token_currency = OracleTokenCurrency {
                id,
                key: (
                    token_currency.token.to_owned(),
                    token_currency.currency.to_owned(),
                ),

                token: token_currency.token.to_owned(),
                currency: token_currency.currency.to_owned(),
                oracle_id,
                weightage: self.weightage,
                block: ctx.block.clone(),
            };
            services
                .oracle_token_currency
                .by_key
                .put(&oracle_token_currency.key, &oracle_token_currency.id)?;
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
        //delete for oracle data from oracle
        services.oracle.by_id.delete(&oracle_id)?;
        let previous_hsitory = get_previous_oracle_history_list(services, oracle_id);
        match previous_hsitory {
            Ok(previous_oracle) => {
                for oracle_history in &previous_oracle {
                    for price_feed_item in &oracle_history.price_feeds {
                        let deletion_key = (
                            price_feed_item.token.to_owned(),
                            price_feed_item.currency.to_owned(),
                            oracle_history.oracle_id,
                        );
                        match services.oracle_token_currency.by_id.delete(&deletion_key) {
                            Ok(_) => {
                                // Successfully deleted
                            }
                            Err(err) => {
                                let error_message = format!("Error: remove_oracle: {:?}", err);
                                eprintln!("{}", error_message);
                                return Err(Error::DBError(ain_db::DBError::Custom(err.into())));
                            }
                        }
                    }
                }
            }
            Err(err) => {
                let error_message = format!("Error: remove_oracle: {:?}", err);
                eprintln!("{}", error_message);
                return Err(Error::NotFound(NotFoundKind::Oracle));
            }
        }
        Ok(())
    }
    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let oracle_id = context.tx.txid;
        let previous_oracle_history_result = get_previous_oracle_history_list(services, oracle_id);

        match previous_oracle_history_result {
            Ok(previous_oracle_history) => {
                for previous_oracle in previous_oracle_history {
                    let oracle = Oracle {
                        id: previous_oracle.oracle_id,
                        owner_address: previous_oracle.owner_address,
                        weightage: previous_oracle.weightage,
                        price_feeds: previous_oracle.price_feeds.clone(),
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
                            oracle_id,
                            weightage: oracle.weightage,
                            block: oracle.block.clone(),
                        };

                        services
                            .oracle_token_currency
                            .by_id
                            .put(&oracle_token_currency.id, &oracle_token_currency)?;
                    }
                }
            }
            Err(err) => {
                eprintln!("Error remove_oracle invalidate : {:?}", err);
                return Err(Error::from(err));
            }
        }

        Ok(())
    }
}

impl Index for UpdateOracle {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        let oracle_id = ctx.tx.txid;
        let price_feeds_items: Vec<PriceFeedsItem> = self
            .price_feeds
            .iter()
            .map(|pair| PriceFeedsItem {
                token: pair.token.clone(),
                currency: pair.currency.clone(),
            })
            .collect();

        let oracle = Oracle {
            id: oracle_id,
            owner_address: self.script.to_hex_string(),
            weightage: self.weightage,
            price_feeds: price_feeds_items,
            block: ctx.block.clone(),
        };

        //save oracle
        services.oracle.by_id.put(&oracle.id, &oracle)?;
        let previous_oracle_history_result = get_previous_oracle_history_list(services, oracle_id);
        match previous_oracle_history_result {
            Ok(previous_oracle) => {
                for oracle in previous_oracle {
                    for price_feed_item in &oracle.price_feeds {
                        let deletion_id = (
                            price_feed_item.token.clone(),
                            price_feed_item.currency.clone(),
                            oracle_id,
                        );
                        match services.oracle_token_currency.by_id.delete(&deletion_id) {
                            Ok(_) => {
                                // Successfully deleted
                            }
                            Err(err) => {
                                let error_message = format!("Error:update oracle: {:?}", err);
                                eprintln!("{}", error_message);
                                return Err(Error::DBError(ain_db::DBError::Custom(err.into())));
                            }
                        }
                        let deletion_key = (
                            price_feed_item.token.clone(),
                            price_feed_item.currency.clone(),
                        );
                        match services.oracle_token_currency.by_key.delete(&deletion_key) {
                            Ok(_) => {
                                // Successfully deleted
                            }
                            Err(err) => {
                                let error_message = format!("Error: update_oracle: {:?}", err);
                                eprintln!("{}", error_message);
                                return Err(Error::DBError(ain_db::DBError::Custom(err.into())));
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
                oracle_id,
                weightage: self.weightage,
                block: ctx.block.clone(),
            };

            services
                .oracle_token_currency
                .by_key
                .put(&oracle_token_currency.key, &oracle_token_currency.id)?;
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
            .by_key
            .put(&oracle_history.oracle_id, &oracle_history.id)?;
        services
            .oracle_history
            .by_id
            .put(&oracle_history.id, &oracle_history)?;

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let oracle_id = context.tx.txid;
        services.oracle_history.by_key.delete(&oracle_id)?;
        services.oracle_history.by_id.delete(&(
            oracle_id,
            context.block.height,
            context.tx.txid,
        ))?;

        let prices_feeds = self.price_feeds.as_ref();
        for pair in prices_feeds {
            services.oracle_token_currency.by_id.delete(&(
                pair.token.to_string(),
                pair.token.to_string(),
                self.oracle_id,
            ))?;
        }
        let previous_oracle_history_result = get_previous_oracle_history_list(services, oracle_id);
        match previous_oracle_history_result {
            Ok(previous_oracle_result) => {
                for previous_oracle in previous_oracle_result {
                    for price_feed_item in &previous_oracle.price_feeds {
                        let deletion_key = (
                            price_feed_item.token.clone(),
                            price_feed_item.currency.clone(),
                            previous_oracle.oracle_id,
                        );

                        match services.oracle_token_currency.by_id.delete(&deletion_key) {
                            Ok(_) => {
                                // Successfully deleted
                            }
                            Err(err) => {
                                let error_message =
                                    format!("Error updating oracle invalidate: {:?}", err);
                                eprintln!("{}", error_message);
                                return Err(Error::DBError(ain_db::DBError::Custom(err.into())));
                            }
                        }
                    }
                }
            }
            Err(err) => {
                let error_message = format!("Error updating oracle invalidate: {:?}", err);
                eprintln!("{}", error_message);
                return Err(Error::NotFound(NotFoundKind::Oracle));
            }
        }
        Ok(())
    }
}

impl Index for SetOracleData {
    fn index(self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let set_oracle_data = SetOracleData {
            oracle_id: self.oracle_id,
            timestamp: self.timestamp,
            token_prices: self.token_prices,
        };
        let feeds = map_price_feeds(set_oracle_data, context)?;
        let mut pairs: Vec<(String, String)> = Vec::new();
        for feed in feeds {
            pairs.push((feed.token.clone(), feed.currency.clone()));

            if services.oracle_price_feed.by_key.get(&feed.key).is_err() {
                services.oracle_price_feed.by_key.put(&feed.key, &feed.id)?;
            }
            if services.oracle_price_feed.by_id.get(&feed.id).is_err() {
                services.oracle_price_feed.by_id.put(&feed.id, &feed)?;
            }
        }
        let intervals: Vec<OracleIntervalSeconds> = vec![
            OracleIntervalSeconds::FifteenMinutes,
            OracleIntervalSeconds::OneHour,
            OracleIntervalSeconds::OneDay,
        ];
        for (token, currency) in pairs.iter() {
            let aggregated_value = map_price_aggregated(services, context, token, currency);
            if let Ok(Some(value)) = aggregated_value {
                let aggreated_id = (
                    value.token.clone(),
                    value.currency.clone(),
                    value.block.height,
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

                let price_ticker = PriceTicker {
                    id: aggreated_key,
                    sort: format!(
                        "{}{}{}{}",
                        hex::encode(value.aggregated.oracles.total.to_be_bytes()),
                        hex::encode(value.block.height.to_be_bytes()),
                        value.token.clone(),
                        value.currency.clone(),
                    ),
                    price: value,
                };
                services
                    .price_ticker
                    .by_id
                    .put(&price_ticker.id, &price_ticker)?;

                //SetOracleInterval
                let aggregated = services.oracle_price_aggregated.by_id.get(&(
                    token.to_owned(),
                    currency.to_owned(),
                    context.block.height,
                ))?;
                for interval in intervals.clone() {
                    index_interval_mapper(
                        services,
                        &context.block,
                        token,
                        currency,
                        aggregated.as_ref().unwrap(),
                        &interval,
                    )?;
                }
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
        let intervals: Vec<OracleIntervalSeconds> = vec![
            OracleIntervalSeconds::FifteenMinutes,
            OracleIntervalSeconds::OneHour,
            OracleIntervalSeconds::OneDay,
        ];
        let feeds = map_price_feeds(set_oracle_data, context)?;
        let mut pairs: Vec<(String, String)> = Vec::new();
        for feed in feeds {
            pairs.push((feed.token.clone(), feed.currency.clone()));
            services.oracle_price_feed.by_key.delete(&feed.key)?;
            services.oracle_price_feed.by_id.delete(&feed.id)?;
        }
        for (token, currency) in pairs.iter() {
            let aggreated_id = (token.to_owned(), currency.to_owned(), context.block.height);
            let aggregated_price = services.oracle_price_aggregated.by_id.get(&aggreated_id)?;
            if let Some(aggregated) = aggregated_price {
                for interval in &intervals {
                    let _err = invalidate_oracle_interval(
                        services,
                        &context.block,
                        token,
                        currency,
                        &aggregated,
                        interval,
                    );
                }
            }
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
                .ok_or("Error mapping price aggregated index")?;

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
            Ok(feed_ids) => {
                feed_ids.map_or_else(|| Ok(None), |id| services.oracle_price_feed.by_id.get(&id))
            }
            Err(err) => {
                println!("the err {:?}", err);
                Err(err)
            }
        };

        let oracle_price_feed: Option<OraclePriceFeed> = match feeds {
            Ok(Some(feed)) => Some(feed),
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
        return Ok(None);
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
    set_oracle_data: SetOracleData,
    context: &Context,
) -> Result<Vec<OraclePriceFeed>> {
    // Make sure to define or replace YourErrorType with the actual error type you are using
    let mut result: Vec<OraclePriceFeed> = Vec::new();
    let token_prices = set_oracle_data.token_prices.as_ref(); // Assuming token_prices is accessible directly from set_oracle_data
    for token_price in token_prices {
        for token_amount in token_price.prices.as_ref() {
            let id = (
                token_price.token.clone(),
                token_amount.currency.clone(),
                set_oracle_data.oracle_id,
                context.tx.txid,
            );
            let oracle_price_feed = OraclePriceFeed {
                id,
                key: (
                    token_price.token.clone(),
                    token_amount.currency.clone(),
                    set_oracle_data.oracle_id,
                ),
                sort: hex::encode(context.block.height.to_string() + &context.tx.txid.to_string()),
                amount: token_amount.amount,
                currency: token_amount.currency.clone(),
                block: context.block.clone(),
                oracle_id: set_oracle_data.oracle_id,
                time: set_oracle_data.timestamp as i32,
                token: token_price.token.clone(),
                txid: context.tx.txid,
            };
            result.push(oracle_price_feed);
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
    interval: &OracleIntervalSeconds,
) -> Result<()> {
    let previous_aggrigated_interval = services
        .oracle_price_aggregated_interval
        .by_key
        .list(
            Some((token.to_owned(), currency.to_owned(), interval.clone())),
            SortOrder::Descending,
        )?
        .take(1)
        .map(|item| {
            let (_, id) = item?;
            let price_agrregated_interval = services
                .oracle_price_aggregated_interval
                .by_id
                .get(&id)?
                .ok_or("Missing oracle price aggregated interval index")?;
            Ok(price_agrregated_interval)
        })
        .collect::<Result<Vec<_>>>();

    if let Ok(previous_oracle_price_aggreated) = previous_aggrigated_interval {
        if previous_oracle_price_aggreated.is_empty()
            || (block.median_time - previous_oracle_price_aggreated[0].block.median_time
                > interval.clone() as i64)
        {
            let oracle_price_aggregated_interval = OraclePriceAggregatedInterval {
                id: (
                    token.to_owned(),
                    currency.to_owned(),
                    interval.clone(),
                    block.height,
                ),
                key: (token.to_owned(), currency.to_owned(), interval.clone()),
                sort: aggregated.sort.to_owned(),
                token: token.to_owned(),
                currency: currency.to_owned(),
                aggregated: OraclePriceAggregatedIntervalAggregated {
                    amount: aggregated.aggregated.amount.clone(),
                    weightage: aggregated.aggregated.weightage,
                    count: 1,
                    oracles: OraclePriceAggregatedIntervalAggregatedOracles {
                        active: aggregated.aggregated.oracles.active,
                        total: aggregated.aggregated.oracles.total,
                    },
                },
                block: block.clone(),
            };
            services.oracle_price_aggregated_interval.by_id.put(
                &oracle_price_aggregated_interval.id,
                &oracle_price_aggregated_interval,
            )?;
            services.oracle_price_aggregated_interval.by_key.put(
                &oracle_price_aggregated_interval.key,
                &oracle_price_aggregated_interval.id,
            )?;
        } else {
            process_inner_values(services, &previous_oracle_price_aggreated[0], aggregated);
        }
    } else {
        let err = previous_aggrigated_interval.err();
        match err {
            Some(e) => {
                let error_message = format!("Error updating oracle index interval mapper: {:?}", e);
                eprintln!("{}", error_message);
                return Err(Error::NotFound(NotFoundKind::Oracle));
            }
            None => {
                eprintln!("Unknown index interval mapper error ");
                return Err(Error::NotFound(NotFoundKind::Oracle));
            }
        }
    }

    Ok(())
}

pub fn invalidate_oracle_interval(
    services: &Arc<Services>,
    _block: &BlockContext,
    token: &str,
    currency: &str,
    aggregated: &OraclePriceAggregated,
    interval: &OracleIntervalSeconds,
) -> Result<()> {
    let previous_aggrigated_interval = services
        .oracle_price_aggregated_interval
        .by_key
        .list(
            Some((token.to_owned(), currency.to_owned(), interval.clone())),
            SortOrder::Descending,
        )?
        .take(1)
        .map(|item| {
            let (_, id) = item?;
            let price_agrregated_interval = services
                .oracle_price_aggregated_interval
                .by_id
                .get(&id)?
                .ok_or("Missing oracle price aggregated interval index")?;
            Ok(price_agrregated_interval)
        })
        .collect::<Result<Vec<_>>>();

    if let Ok(oracle_price_aggreated) = previous_aggrigated_interval {
        if oracle_price_aggreated[0].aggregated.count != 1 {
            let _err = services
                .oracle_price_aggregated_interval
                .by_id
                .delete(&oracle_price_aggreated[0].id);
        } else {
            let lastprice = oracle_price_aggreated[0].aggregated.clone();
            let count = lastprice.count - 1;
            let previous_aggregated_interval = OraclePriceAggregatedInterval {
                id: oracle_price_aggreated[0].id.clone(),
                key: oracle_price_aggreated[0].key.clone(),
                sort: oracle_price_aggreated[0].sort.clone(),
                token: oracle_price_aggreated[0].token.clone(),
                currency: oracle_price_aggreated[0].currency.clone(),
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
                    count,
                    oracles: OraclePriceAggregatedIntervalAggregatedOracles {
                        active: backward_aggregate_number(
                            lastprice.oracles.active,
                            aggregated.aggregated.oracles.active,
                            lastprice.count as u32,
                        ),
                        total: backward_aggregate_number(
                            lastprice.oracles.total,
                            aggregated.aggregated.oracles.total,
                            lastprice.count as u32,
                        ),
                    },
                },
                block: oracle_price_aggreated[0].block.clone(),
            };
            let _err = services.oracle_price_aggregated_interval.by_id.put(
                &previous_aggregated_interval.id,
                &previous_aggregated_interval,
            );
            let _err = services.oracle_price_aggregated_interval.by_key.put(
                &previous_aggregated_interval.key,
                &previous_aggregated_interval.id,
            );
        }
    } else {
        let err = previous_aggrigated_interval.err();
        match err {
            Some(e) => {
                let error_message = format!("Error updating oracle  interval: {:?}", e);
                eprintln!("{}", error_message);
                return Err(Error::NotFound(NotFoundKind::Oracle));
            }
            None => {
                eprintln!("Unknown previous_aggrigated_interval error ");
                return Err(Error::NotFound(NotFoundKind::Oracle));
            }
        }
    }
    Ok(())
}

fn process_inner_values(
    services: &Arc<Services>,
    previous_data: &OraclePriceAggregatedInterval,
    aggregated: &OraclePriceAggregated,
) {
    let lastprice = previous_data.aggregated.clone();
    let count = lastprice.count + 1;

    let aggregated_interval = OraclePriceAggregatedInterval {
        id: previous_data.id.clone(),
        key: previous_data.key.clone(),
        sort: previous_data.sort.clone(),
        token: previous_data.token.clone(),
        currency: previous_data.currency.clone(),
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
            count,
            oracles: OraclePriceAggregatedIntervalAggregatedOracles {
                active: forward_aggregate_number(
                    lastprice.oracles.active,
                    aggregated.aggregated.oracles.active,
                    lastprice.count,
                ),
                total: forward_aggregate_number(
                    lastprice.oracles.total,
                    aggregated.aggregated.oracles.total,
                    lastprice.count,
                ),
            },
        },
        block: previous_data.block.clone(),
    };
    let _err = services
        .oracle_price_aggregated_interval
        .by_id
        .put(&aggregated_interval.id, &aggregated_interval);
    let _err = services
        .oracle_price_aggregated_interval
        .by_key
        .put(&aggregated_interval.key, &aggregated_interval.id);
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

    (last_value_decimal * count_decimal - new_value_decimal) / (count_decimal - Decimal::from(1))
}

fn backward_aggregate_number(last_value: i32, new_value: i32, count: u32) -> i32 {
    let last_value_decimal =
        Decimal::from_str(&last_value.to_string()).unwrap_or_else(|_| Decimal::zero());
    let new_value_decimal =
        Decimal::from_str(&new_value.to_string()).unwrap_or_else(|_| Decimal::zero());
    let count_decimal = Decimal::from(count);

    let result = (last_value_decimal * count_decimal - new_value_decimal)
        / (count_decimal - Decimal::from(1));

    result.to_i32().unwrap_or_else(|| {
        eprintln!("Result is too large to fit into i32, returning 0");
        0
    })
}

fn get_previous_oracle_history_list(
    services: &Arc<Services>,
    oracle_id: Txid,
) -> std::result::Result<Vec<OracleHistory>, Box<dyn std::error::Error>> {
    let history = services
        .oracle_history
        .by_key
        .list(Some(oracle_id), SortOrder::Descending)?
        .map(|item| {
            let (_, id) = item?;
            let b = services
                .oracle_history
                .by_id
                .get(&id)?
                .ok_or("Missing oracle previous history index")?;

            Ok(b)
        })
        .collect::<std::result::Result<Vec<_>, Box<dyn std::error::Error>>>()?;
    Ok(history)
}
