use std::{collections::HashSet, str::FromStr, sync::Arc, vec};

use ain_dftx::{common::CompactVec, oracles::*};
use anyhow::{anyhow, Context as _};
use bitcoin::Txid;
use log::debug;
use rust_decimal::{
    prelude::{ToPrimitive, Zero},
    Decimal,
};
use rust_decimal_macros::dec;

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
        let price_feeds_items = self
            .price_feeds
            .iter()
            .map(|pair| PriceFeedsItem {
                token: pair.token.clone(),
                currency: pair.currency.clone(),
            })
            .collect::<Vec<PriceFeedsItem>>();
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
            price_feeds: price_feeds_items.clone(),
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

        let prices_feeds = price_feeds_items;
        for token_currency in prices_feeds {
            let id = (
                token_currency.token.clone(),
                token_currency.currency.clone(),
                oracle_id,
            );

            let oracle_token_currency = OracleTokenCurrency {
                id,
                key: (
                    token_currency.token.to_owned(),
                    token_currency.currency.to_owned(),
                    ctx.block.height,
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
        services.oracle_history.by_key.delete(&(oracle_id))?;
        for currency_pair in self.price_feeds.as_ref().iter() {
            let token_currency_id = (
                currency_pair.token.to_owned(),
                currency_pair.currency.to_owned(),
                oracle_id,
            );
            let token_currency_key = (
                currency_pair.token.to_owned(),
                currency_pair.currency.to_owned(),
                context.block.height,
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
        services.oracle.by_id.delete(&oracle_id)?;
        let previous_hsitory = get_previous_oracle_history_list(services, oracle_id);
        match previous_hsitory {
            Ok(previous_oracle) => {
                for oracle_history in &previous_oracle {
                    for price_feed_item in &oracle_history.price_feeds {
                        let deletion_id = (
                            price_feed_item.token.to_owned(),
                            price_feed_item.currency.to_owned(),
                            oracle_history.oracle_id,
                        );
                        let deletion_key = (
                            price_feed_item.token.to_owned(),
                            price_feed_item.currency.to_owned(),
                            oracle_history.block.height,
                        );
                        services.oracle_token_currency.by_id.delete(&deletion_id)?;
                        services
                            .oracle_token_currency
                            .by_key
                            .delete(&deletion_key)?;
                    }
                }
            }
            Err(err) => {
                return Err(Error::Other(anyhow!("remove oracle : {}", err)));
            }
        }
        Ok(())
    }
    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let oracle_id = context.tx.txid;
        let previous_oracle_history_result = get_previous_oracle_history_list(services, oracle_id);
        let previous_oracle_history = previous_oracle_history_result?;

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
                        context.block.height,
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

                services
                    .oracle_token_currency
                    .by_key
                    .put(&oracle_token_currency.key, &oracle_token_currency.id)?;
            }
        }

        Ok(())
    }
}

impl Index for UpdateOracle {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        let oracle_id = ctx.tx.txid;
        let price_feeds_items = self
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
        let previous_oracle = previous_oracle_history_result?;

        for oracle in previous_oracle {
            for price_feed_item in &oracle.price_feeds {
                let deletion_id = (
                    price_feed_item.token.clone(),
                    price_feed_item.currency.clone(),
                    oracle_id,
                );
                services.oracle_token_currency.by_id.delete(&deletion_id)?;
                let deletion_key = (
                    price_feed_item.token.clone(),
                    price_feed_item.currency.clone(),
                    ctx.block.height,
                );
                services
                    .oracle_token_currency
                    .by_key
                    .delete(&deletion_key)?;
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
                    ctx.block.height,
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
        let previous_oracle_result = previous_oracle_history_result?;

        for previous_oracle in previous_oracle_result {
            let oracle = Oracle {
                id: previous_oracle.oracle_id,
                owner_address: previous_oracle.owner_address,
                weightage: previous_oracle.weightage,
                price_feeds: previous_oracle.price_feeds.clone(),
                block: previous_oracle.block.clone(),
            };
            services.oracle.by_id.put(&oracle.id, &oracle)?;
            for prev_token_currency in &previous_oracle.price_feeds {
                let oracle_token_currency = OracleTokenCurrency {
                    id: (
                        prev_token_currency.token.clone(),
                        prev_token_currency.currency.clone(),
                        oracle.id,
                    ),
                    key: (
                        prev_token_currency.token.clone(),
                        prev_token_currency.currency.clone(),
                        context.block.height,
                    ),
                    token: prev_token_currency.token.clone(),
                    currency: prev_token_currency.currency.to_owned(),
                    oracle_id,
                    weightage: oracle.weightage,
                    block: oracle.block.clone(),
                };

                services
                    .oracle_token_currency
                    .by_id
                    .put(&oracle_token_currency.id, &oracle_token_currency)?;

                services
                    .oracle_token_currency
                    .by_key
                    .put(&oracle_token_currency.key, &oracle_token_currency.id)?;
            }
        }
        Ok(())
    }
}

fn map_price_aggregated(services: &Arc<Services>, context: &Context, pair: (String, String, Txid)) -> Result<Option<OraclePriceAggregated>> {
    let (token, currency, _) = pair;
    let oracle_repo = &services.oracle_token_currency;
    let feed_repo = &services.oracle_price_feed;

    let keys_ids = oracle_repo
        .by_key
        .list(
            Some((token.clone(), currency.clone(), u32::MAX)),
            SortOrder::Descending,
        )?
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == token && k.1 == currency,
            _ => true,
        })
        .flatten()
        .collect::<Vec<_>>();

    let mut oracles = Vec::new();
    for (_, id) in keys_ids {
        let oracle = oracle_repo
            .by_id
            .get(&id)?;
        let Some(oracle) = oracle else {
            continue
        };
        oracles.push(oracle);
    }

    let mut aggregated_total = Decimal::zero();
    let mut aggregated_count = 0;
    let mut aggregated_weightage = 0;

    let oracles_len = oracles.len();
    for oracle in oracles {
        if oracle.weightage == 0 {
            debug!("Skipping oracle with zero weightage: {:?}", oracle);
            continue;
        }

        let feed_id = feed_repo
            .by_key
            .get(&(oracle.token, oracle.currency, oracle.oracle_id))?;

        let Some(feed_id) = feed_id else {
            continue
        };

        let feed = feed_repo
            .by_id
            .get(&feed_id)?;

        let Some(feed) = feed else {
            continue
        };

        let time_diff = Decimal::from(feed.time) - Decimal::from(context.block.time);
        if Decimal::abs(&time_diff) < dec!(3600) {
            aggregated_count += 1;
            aggregated_weightage += oracle.weightage;
            log::debug!(
                "SetOracleData weightage: {:?} * oracle_price.amount: {:?}",
                aggregated_weightage,
                feed.amount
            );
            let weighted_amount = Decimal::from(feed.amount)
                .checked_mul(Decimal::from(oracle.weightage))
                .context("weighted_amount overflow")?;
            aggregated_total += weighted_amount;
        }
    }

    if aggregated_count == 0 {
        return Ok(None)
    }

    let aggregated_amount = aggregated_total
        .checked_div(Decimal::from(aggregated_weightage))
        .context("aggregated_amount underflow")?;

    let key = (token.clone(), currency.clone());
    Ok(Some(OraclePriceAggregated {
        key: key.clone(),
        id: (key.0, key.1, context.block.height),
        sort: format!(
            "{}{}",
            hex::encode(context.block.median_time.to_be_bytes()),
            hex::encode(context.block.height.to_be_bytes())
        ),
        token,
        currency,
        aggregated: OraclePriceAggregatedAggregated {
            amount: format!("{:.8}", aggregated_amount),
            weightage: aggregated_weightage as i32,
            oracles: OraclePriceAggregatedAggregatedOracles {
                active: aggregated_count,
                total: oracles_len as i32,
            },
        },
        block: context.block.clone(),
    }))
}

fn index_set_oracle_data(services: &Arc<Services>, context: &Context, pairs: HashSet<(String, String, Txid)>) -> Result<()> {
    let oracle_repo = &services.oracle_price_aggregated;
    let ticker_repo = &services.price_ticker;

    for pair in pairs.into_iter() {
        let price_aggregated = map_price_aggregated(services, context, pair)?;

        let Some(price_aggregated) = price_aggregated else {
            continue
        };

        let key = (price_aggregated.token.clone(), price_aggregated.currency.clone());
        let id = (key.0.clone(), key.1.clone(), price_aggregated.block.height);
        oracle_repo
            .by_key
            .put(&key, &id)?;
        oracle_repo
            .by_id
            .put(&id, &price_aggregated)?;

        ticker_repo
            .by_id
            .put(&key.clone(), &PriceTicker {
                sort: format!(
                    "{}{}{}{}",
                    hex::encode(price_aggregated.aggregated.oracles.total.to_be_bytes()),
                    hex::encode(price_aggregated.block.height.to_be_bytes()),
                    key.0.clone(),
                    key.1.clone(),
                ),
                id: key,
                price: price_aggregated,
            })?;
    }
    Ok(())
}

fn index_set_oracle_data_interval(services: &Arc<Services>, context: &Context, pairs: HashSet<(String, String, Txid)>) -> Result<()> {
    let intervals = vec![
        OracleIntervalSeconds::FifteenMinutes,
        OracleIntervalSeconds::OneHour,
        OracleIntervalSeconds::OneDay,
    ];

    for (token, currency, _) in pairs.into_iter() {
        let aggregated = services
            .oracle_price_aggregated
            .by_id
            .get(&(token.clone(), currency.clone(), context.block.height))?;

        let Some(aggregated) = aggregated else {
           continue
        };

        for interval in intervals.clone() {
            index_interval_mapper(
                services,
                &context.block,
                token.clone(),
                currency.clone(),
                &aggregated,
                interval,
            )?;
        }
    }

    Ok(())
}

impl Index for SetOracleData {
    fn index(self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let feed_repo = &services.oracle_price_feed;

        let mut pairs = HashSet::new();
        let feeds = map_price_feeds(&self, context);
        for feed in &feeds {
            pairs.insert((feed.token.clone(), feed.currency.clone(), feed.oracle_id));
            feed_repo.by_key.put(&feed.key, &feed.id)?;
            feed_repo.by_id.put(&feed.id, feed)?;
        }

        index_set_oracle_data(services, context, pairs.clone())?;

        index_set_oracle_data_interval(services, context, pairs)?;

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let set_oracle_data = SetOracleData {
            oracle_id: self.oracle_id,
            timestamp: self.timestamp,
            token_prices: CompactVec::from(Vec::new()),
        };
        let intervals = vec![
            OracleIntervalSeconds::FifteenMinutes,
            OracleIntervalSeconds::OneHour,
            OracleIntervalSeconds::OneDay,
        ];
        let feeds = map_price_feeds(&set_oracle_data, context);
        let mut pairs: Vec<(String, String)> = Vec::new();
        for feed in feeds {
            pairs.push((feed.token.clone(), feed.currency.clone()));
            services.oracle_price_feed.by_id.delete(&feed.id)?;
            services.oracle_price_feed.by_key.delete(&feed.key)?;
        }

        for (token, currency) in pairs.iter() {
            let aggreated_id = (token.to_owned(), currency.to_owned(), context.block.height);
            let aggreated_key = (token.to_owned(), currency.to_owned());
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
            services
                .oracle_price_aggregated
                .by_key
                .delete(&aggreated_key)?;
        }
        Ok(())
    }
}

fn map_price_feeds(
    data: &SetOracleData,
    ctx: &Context,
) -> Vec<OraclePriceFeed> {
    let mut feeds = Vec::new();
    let token_prices = data.token_prices.as_ref();
    for token_price in token_prices {
        for token_amount in token_price.prices.as_ref() {
            let token = token_price.token.clone();
            let currency = token_amount.currency.clone();
            let key = (token.clone(), currency.clone(), data.oracle_id);

            let oracle_price_feed = OraclePriceFeed {
                key: key.clone(),
                id: (key.0, key.1, key.2, ctx.tx.txid),
                sort: hex::encode(ctx.block.height.to_string() + &ctx.tx.txid.to_string()),
                amount: token_amount.amount,
                currency,
                block: ctx.block.clone(),
                oracle_id: data.oracle_id,
                time: data.timestamp as i32,
                token,
                txid: ctx.tx.txid,
            };
            feeds.push(oracle_price_feed);
        }
    }
    feeds
}

fn start_new_bucket(
    services: &Arc<Services>,
    block: &BlockContext,
    token: String,
    currency: String,
    aggregated: &OraclePriceAggregated,
    interval: OracleIntervalSeconds,
) -> Result<()> {
    let key = (token.clone(), currency.clone(), interval);
    let id = (key.0.clone(), key.1.clone(), key.2.clone(), block.height);
    let repo = &services.oracle_price_aggregated_interval;
    repo
        .by_id
        .put(
        &id,
        &OraclePriceAggregatedInterval {
            id: id.clone(),
            key: key.clone(),
            sort: aggregated.sort.clone(),
            token,
            currency,
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
        },
    )?;
    repo
        .by_key
        .put(&key, &id)?;

    Ok(())
}

pub fn index_interval_mapper(
    services: &Arc<Services>,
    block: &BlockContext,
    token: String,
    currency: String,
    aggregated: &OraclePriceAggregated,
    interval: OracleIntervalSeconds,
) -> Result<()> {
    let repo = &services.oracle_price_aggregated_interval;
    let previous = repo
        .by_key
        .list(
            Some((token.clone(), currency.clone(), interval.clone())),
            SortOrder::Descending,
        )?
        .take(1)
        .flatten()
        .collect::<Vec<_>>();

    if previous.is_empty() {
        return start_new_bucket(
            services,
            block,
            token.clone(),
            currency.clone(),
            aggregated,
            interval,
        )
    }

    for (_, id) in previous {
        let aggregated_interval = repo
            .by_id
            .get(&id)?;
        if let Some(aggregated_interval) = aggregated_interval {
            if block.median_time - aggregated.block.median_time > interval.clone() as i64 {
                return start_new_bucket(
                    services,
                    block,
                    token.clone(),
                    currency.clone(),
                    aggregated,
                    interval,
                )
            }

            forward_aggregate(services, &aggregated_interval, aggregated)?;
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
    let previous = services
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
                .context("Missing oracle price aggregated interval index")?;
            Ok(price_agrregated_interval)
        })
        .collect::<Result<Vec<_>>>();

    if let Ok(oracle_price_aggreated) = previous {
        if oracle_price_aggreated[0].aggregated.count != 1 {
            let _err = services
                .oracle_price_aggregated_interval
                .by_id
                .delete(&oracle_price_aggreated[0].id);
        } else {
            let last_price = oracle_price_aggreated[0].aggregated.clone();
            let count = last_price.count - 1;

            let aggregated_amount = backward_aggregate_value(
                Decimal::from_str(&last_price.amount)?,
                Decimal::from_str(&aggregated.aggregated.amount)?,
                Decimal::from(count),
            )?;

            let aggregated_weightage = backward_aggregate_value(
                Decimal::from(last_price.weightage),
                Decimal::from(aggregated.aggregated.weightage),
                Decimal::from(count),
            )?;

            let aggregated_active = backward_aggregate_value(
                Decimal::from(last_price.oracles.active),
                Decimal::from(aggregated.aggregated.oracles.active),
                Decimal::from(last_price.count),
            )?;

            let aggregated_total = backward_aggregate_value(
                Decimal::from(last_price.oracles.total),
                Decimal::from(aggregated.aggregated.oracles.total),
                Decimal::from(last_price.count),
            )?;

            let previous_aggregated_interval = OraclePriceAggregatedInterval {
                id: oracle_price_aggreated[0].id.clone(),
                key: oracle_price_aggreated[0].key.clone(),
                sort: oracle_price_aggreated[0].sort.clone(),
                token: oracle_price_aggreated[0].token.clone(),
                currency: oracle_price_aggreated[0].currency.clone(),
                aggregated: OraclePriceAggregatedIntervalAggregated {
                    amount: aggregated_amount.to_string(),
                    weightage: aggregated_weightage.to_i32().context("Err: Decimal.to_i32()")?,
                    count,
                    oracles: OraclePriceAggregatedIntervalAggregatedOracles {
                        active: aggregated_active.to_i32().context("Err: Decimal.to_i32()")?,
                        total: aggregated_total.to_i32().context("Err: Decimal.to_i32()")?,
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
        let err = previous.err();
        match err {
            Some(e) => {
                return Err(Error::Other(anyhow!("oracle invalidate interval : {}", e)));
            }
            None => {
                return Err(Error::NotFound(NotFoundKind::Oracle));
            }
        }
    }
    Ok(())
}

fn forward_aggregate(
    services: &Arc<Services>,
    previous: &OraclePriceAggregatedInterval,
    aggregated: &OraclePriceAggregated,
) -> Result<()> {
    let last_price = previous.aggregated.clone();
    let count = last_price.count + 1;

    let aggregated_amount = forward_aggregate_value(
        Decimal::from_str(&last_price.amount)?,
        Decimal::from_str(&aggregated.aggregated.amount)?,
        Decimal::from(count),
    )?;

    let aggregated_weightage = forward_aggregate_value(
        Decimal::from(last_price.weightage),
        Decimal::from(aggregated.aggregated.weightage),
        Decimal::from(count),
    )?;

    let aggregated_active = forward_aggregate_value(
        Decimal::from(last_price.oracles.active),
        Decimal::from(aggregated.aggregated.oracles.active),
        Decimal::from(last_price.count),
    )?;

    let aggregated_total = forward_aggregate_value(
        Decimal::from(last_price.oracles.total),
        Decimal::from(aggregated.aggregated.oracles.total),
        Decimal::from(last_price.count),
    )?;

    let aggregated_interval = OraclePriceAggregatedInterval {
        id: previous.id.clone(),
        key: previous.key.clone(),
        sort: previous.sort.clone(),
        token: previous.token.clone(),
        currency: previous.currency.clone(),
        aggregated: OraclePriceAggregatedIntervalAggregated {
            amount: aggregated_amount.to_string(),
            weightage: aggregated_weightage.to_i32().context("Err: Decimal.to_i32()")?,
            count,
            oracles: OraclePriceAggregatedIntervalAggregatedOracles {
                active: aggregated_active.to_i32().context("Err: Decimal.to_i32()")?,
                total: aggregated_total.to_i32().context("Err: Decimal.to_i32()")?,
            },
        },
        block: previous.block.clone(),
    };
    services
        .oracle_price_aggregated_interval
        .by_id
        .put(&aggregated_interval.id, &aggregated_interval)?;
    services
        .oracle_price_aggregated_interval
        .by_key
        .put(&aggregated_interval.key, &aggregated_interval.id)?;
    Ok(())
}

fn forward_aggregate_value(last_value: Decimal, new_value: Decimal, count: Decimal) -> Result<Decimal> {
    (last_value * count + new_value)
        .checked_div(count + dec!(1))
        .ok_or_else(|| Error::UnderflowError)
}

fn backward_aggregate_value(last_value: Decimal, new_value: Decimal, count: Decimal) -> Result<Decimal> {
    (last_value * count - new_value)
        .checked_div(count - dec!(1))
        .ok_or_else(|| Error::UnderflowError)
}

fn get_previous_oracle_history_list(
    services: &Arc<Services>,
    oracle_id: Txid,
) -> Result<Vec<OracleHistory>> {
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
                .context("Missing oracle previous history index")?;

            Ok(b)
        })
        .collect::<Result<Vec<_>>>()?;
    Ok(history)
}
