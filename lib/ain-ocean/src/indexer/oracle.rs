use std::{collections::HashSet, sync::Arc};

use ain_dftx::{oracles::*, Currency, Token};
use bitcoin::{hashes::Hash, Txid};
use log::trace;
use rust_decimal::{
    prelude::{ToPrimitive, Zero},
    Decimal,
};
use rust_decimal_macros::dec;
use snafu::OptionExt;

use crate::{
    error::{
        ArithmeticOverflowSnafu, ArithmeticUnderflowSnafu, Error, IndexAction, OtherSnafu,
        ToPrimitiveSnafu,
    },
    indexer::{Context, Index, Result},
    model::{
        BlockContext, Oracle, OracleHistoryId, OracleIntervalSeconds, OraclePriceActiveNext,
        OraclePriceActiveNextOracles, OraclePriceAggregated, OraclePriceAggregatedInterval,
        OraclePriceAggregatedIntervalAggregated, OraclePriceAggregatedIntervalAggregatedOracles,
        OraclePriceAggregatedIntervalId, OraclePriceFeed, OraclePriceFeedId, OracleTokenCurrency,
        PriceFeed, PriceTicker,
    },
    storage::{RepositoryOps, SortOrder},
    Services,
};

pub const AGGREGATED_INTERVALS: [OracleIntervalSeconds; 3] = [
    OracleIntervalSeconds::FifteenMinutes,
    OracleIntervalSeconds::OneDay,
    OracleIntervalSeconds::OneHour,
];

impl Index for AppointOracle {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        let oracle_id = ctx.tx.txid;
        let price_feeds = self
            .price_feeds
            .iter()
            .map(|pair| PriceFeed {
                token: pair.token.clone(),
                currency: pair.currency.clone(),
            })
            .collect::<Vec<_>>();

        let oracle = Oracle {
            owner_address: self.script.to_hex_string(),
            weightage: self.weightage,
            price_feeds: price_feeds.clone(),
            block: ctx.block.clone(),
        };
        services.oracle.by_id.put(&oracle_id, &oracle)?;

        let oracle_history_id = (oracle_id, ctx.block.height);
        services
            .oracle_history
            .by_id
            .put(&oracle_history_id, &oracle)?;

        for token_currency in price_feeds {
            let id = (
                token_currency.token.clone(),
                token_currency.currency.clone(),
                oracle_id,
            );

            let oracle_token_currency = OracleTokenCurrency {
                weightage: self.weightage,
                block: ctx.block.clone(),
            };

            services
                .oracle_token_currency
                .by_id
                .put(&id, &oracle_token_currency)?;
        }

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        trace!("[AppointOracle] Invalidating...");
        let oracle_id = context.tx.txid;
        services.oracle.by_id.delete(&oracle_id)?;

        services
            .oracle_history
            .by_id
            .delete(&(oracle_id, context.block.height))?;

        for currency_pair in self.price_feeds.as_ref() {
            let token_currency_id = (
                currency_pair.token.clone(),
                currency_pair.currency.clone(),
                oracle_id,
            );
            services
                .oracle_token_currency
                .by_id
                .delete(&token_currency_id)?;
        }
        Ok(())
    }
}

impl Index for RemoveOracle {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        let oracle_id = ctx.tx.txid;
        services.oracle.by_id.delete(&oracle_id)?;

        let (_, previous) = get_previous_oracle(services, oracle_id)?;

        for price_feed in &previous.price_feeds {
            services.oracle_token_currency.by_id.delete(&(
                price_feed.token.to_owned(),
                price_feed.currency.to_owned(),
                oracle_id,
            ))?;
        }

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        trace!("[RemoveOracle] Invalidating...");
        let oracle_id = context.tx.txid;
        let (_, previous) = get_previous_oracle(services, oracle_id)?;

        let oracle = Oracle {
            owner_address: previous.owner_address,
            weightage: previous.weightage,
            price_feeds: previous.price_feeds.clone(),
            block: previous.block,
        };

        services.oracle.by_id.put(&oracle_id, &oracle)?;

        for price_feed in previous.price_feeds {
            let oracle_token_currency = OracleTokenCurrency {
                weightage: oracle.weightage,
                block: oracle.block.clone(),
            };

            services.oracle_token_currency.by_id.put(
                &(price_feed.token, price_feed.currency, oracle_id),
                &oracle_token_currency,
            )?;
        }

        Ok(())
    }
}

impl Index for UpdateOracle {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        let oracle_id = ctx.tx.txid;
        let price_feeds = self
            .price_feeds
            .iter()
            .map(|pair| PriceFeed {
                token: pair.token.clone(),
                currency: pair.currency.clone(),
            })
            .collect::<Vec<_>>();

        let oracle = Oracle {
            owner_address: self.script.to_hex_string(),
            weightage: self.weightage,
            price_feeds: price_feeds.clone(),
            block: ctx.block.clone(),
        };
        services.oracle.by_id.put(&oracle_id, &oracle)?;
        services
            .oracle_history
            .by_id
            .put(&(oracle_id, ctx.block.height), &oracle)?;

        let (_, previous) = get_previous_oracle(services, oracle_id)?;
        for price_feed in &previous.price_feeds {
            services.oracle_token_currency.by_id.delete(&(
                price_feed.token.to_owned(),
                price_feed.currency.to_owned(),
                oracle_id,
            ))?;
        }

        for price_feed in price_feeds {
            let oracle_token_currency = OracleTokenCurrency {
                weightage: self.weightage,
                block: ctx.block.clone(),
            };
            services.oracle_token_currency.by_id.put(
                &(price_feed.token, price_feed.currency, oracle_id),
                &oracle_token_currency,
            )?;
        }

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        trace!("[UpdateOracle] Invalidating...");
        let oracle_id = context.tx.txid;
        services
            .oracle_history
            .by_id
            .delete(&(oracle_id, context.block.height))?;

        let price_feeds = self.price_feeds.as_ref();
        for pair in price_feeds {
            services.oracle_token_currency.by_id.delete(&(
                pair.token.clone(),
                pair.currency.clone(),
                self.oracle_id,
            ))?;
        }
        let ((prev_oracle_id, _), previous) = get_previous_oracle(services, oracle_id)?;

        let prev_oracle = Oracle {
            owner_address: previous.owner_address,
            weightage: previous.weightage,
            price_feeds: previous.price_feeds.clone(),
            block: previous.block.clone(),
        };
        services.oracle.by_id.put(&(prev_oracle_id), &prev_oracle)?;

        for price_feed in &previous.price_feeds {
            let oracle_token_currency = OracleTokenCurrency {
                weightage: previous.weightage,
                block: previous.block.clone(),
            };
            services.oracle_token_currency.by_id.put(
                &(
                    price_feed.token.to_owned(),
                    price_feed.currency.to_owned(),
                    prev_oracle_id,
                ),
                &oracle_token_currency,
            )?;
        }

        Ok(())
    }
}

fn map_price_aggregated(
    services: &Arc<Services>,
    context: &Context,
    pair: &(String, String),
) -> Result<Option<OraclePriceAggregated>> {
    let (token, currency) = pair;
    let oracle_repo = &services.oracle_token_currency;
    let feed_repo = &services.oracle_price_feed;

    let oracles = oracle_repo
        .by_id
        .list(
            Some((
                token.clone(),
                currency.clone(),
                Txid::from_byte_array([0xffu8; 32]),
            )),
            SortOrder::Descending,
        )?
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == token.clone() && k.1 == currency.clone(),
            _ => true,
        })
        .flatten()
        .collect::<Vec<_>>();

    let mut aggregated_total = Decimal::zero();
    let mut aggregated_count = Decimal::zero();
    let mut aggregated_weightage = Decimal::zero();

    let oracles_len = oracles.len();
    for (id, oracle) in oracles {
        if oracle.weightage == 0 {
            trace!("Skipping oracle with zero weightage: {:?}", oracle);
            continue;
        }

        let feed_id = feed_repo.by_key.get(&(id))?;

        let Some(feed_id) = feed_id else { continue };

        let feed = feed_repo.by_id.get(&feed_id)?;

        let Some(feed) = feed else { continue };

        let time_diff = Decimal::from(feed.time) - Decimal::from(context.block.time);
        if Decimal::abs(&time_diff) < dec!(3600) {
            aggregated_count = aggregated_count
                .checked_add(dec!(1))
                .context(ArithmeticOverflowSnafu)?;
            aggregated_weightage = aggregated_weightage
                .checked_add(Decimal::from(oracle.weightage))
                .context(ArithmeticOverflowSnafu)?;
            log::trace!(
                "SetOracleData weightage: {:?} * oracle_price.amount: {:?}",
                aggregated_weightage,
                feed.amount
            );
            let weighted_amount = Decimal::from(feed.amount)
                .checked_mul(Decimal::from(oracle.weightage))
                .context(ArithmeticOverflowSnafu)?;
            aggregated_total += weighted_amount;
        }
    }

    if aggregated_count == dec!(0) {
        return Ok(None);
    }

    // NOTE(canonbrother): default by zero since it has not executed within the bucket yet
    let aggregated_amount = aggregated_total
        .checked_div(aggregated_weightage)
        .unwrap_or_default();

    Ok(Some(OraclePriceAggregated {
        aggregated: OraclePriceActiveNext {
            amount: aggregated_amount,
            weightage: aggregated_weightage,
            oracles: OraclePriceActiveNextOracles {
                active: aggregated_count,
                total: oracles_len as i32,
            },
        },
        block: context.block.clone(),
    }))
}

fn index_set_oracle_data(
    services: &Arc<Services>,
    context: &Context,
    pairs: &HashSet<(Token, Currency)>,
) -> Result<()> {
    let oracle_repo = &services.oracle_price_aggregated;
    let ticker_repo = &services.price_ticker;

    for pair in pairs {
        let price_aggregated = map_price_aggregated(services, context, pair)?;

        let Some(price_aggregated) = price_aggregated else {
            continue;
        };

        let token = pair.0.clone();
        let currency = pair.1.clone();

        let id = (
            token.clone(),
            currency.clone(),
            price_aggregated.block.height,
        );
        oracle_repo.by_key.put(pair, &id)?;
        oracle_repo.by_id.put(&id, &price_aggregated)?;

        let key = (
            price_aggregated.aggregated.oracles.total,
            price_aggregated.block.height,
            token.clone(),
            currency.clone(),
        );
        ticker_repo.by_key.put(&key, pair)?;
        ticker_repo.by_id.put(
            &pair.clone(),
            &PriceTicker {
                price: price_aggregated,
            },
        )?;
    }
    Ok(())
}

fn index_set_oracle_data_interval(
    services: &Arc<Services>,
    context: &Context,
    pairs: &HashSet<(String, String)>,
) -> Result<()> {
    for (token, currency) in pairs {
        let aggregated = services.oracle_price_aggregated.by_id.get(&(
            token.clone(),
            currency.clone(),
            context.block.height,
        ))?;

        let Some(aggregated) = aggregated else {
            continue;
        };

        for interval in AGGREGATED_INTERVALS {
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
        for (id, feed) in &feeds {
            let token = id.0.clone();
            let currency = id.1.clone();
            let oracle_id = id.2;
            let key = (token.clone(), currency.clone(), oracle_id);
            pairs.insert((token, currency));
            feed_repo.by_key.put(&key, id)?;
            feed_repo.by_id.put(id, feed)?;
        }

        index_set_oracle_data(services, context, &pairs)?;

        index_set_oracle_data_interval(services, context, &pairs)?;

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let oracle_repo = &services.oracle_price_aggregated;

        let feeds = map_price_feeds(self, context);

        for ((token, currency, _, _), _) in &feeds {
            let id = (token.clone(), currency.clone(), context.block.height);

            let aggregated = oracle_repo.by_id.get(&id)?;

            let Some(aggregated) = aggregated else {
                continue;
            };

            for interval in AGGREGATED_INTERVALS {
                invalidate_oracle_interval(
                    services,
                    &context.block,
                    token,
                    currency,
                    &aggregated,
                    &interval,
                )?;
            }

            oracle_repo.by_id.delete(&id)?;
        }
        Ok(())
    }
}

fn map_price_feeds(
    data: &SetOracleData,
    ctx: &Context,
) -> Vec<(OraclePriceFeedId, OraclePriceFeed)> {
    let mut feeds = Vec::new();
    let token_prices = data.token_prices.as_ref();
    for token_price in token_prices {
        for token_amount in token_price.prices.as_ref() {
            let id = (
                token_price.token.clone(),
                token_amount.currency.clone(),
                data.oracle_id,
                ctx.tx.txid,
            );

            let oracle_price_feed = OraclePriceFeed {
                amount: token_amount.amount,
                block: ctx.block.clone(),
                time: data.timestamp as i32,
                txid: ctx.tx.txid,
            };
            feeds.push((id, oracle_price_feed));
        }
    }
    feeds
}

fn start_new_bucket(
    services: &Arc<Services>,
    block: &BlockContext,
    token: Token,
    currency: Currency,
    aggregated: &OraclePriceAggregated,
    interval: OracleIntervalSeconds,
) -> Result<()> {
    let key = (token.clone(), currency.clone(), interval.clone());
    let id = (token, currency, interval, block.height);
    let repo = &services.oracle_price_aggregated_interval;
    repo.by_id.put(
        &id,
        &OraclePriceAggregatedInterval {
            aggregated: OraclePriceAggregatedIntervalAggregated {
                amount: aggregated.aggregated.amount,
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
    repo.by_key.put(&key, &id)?;

    Ok(())
}

pub fn index_interval_mapper(
    services: &Arc<Services>,
    block: &BlockContext,
    token: Token,
    currency: Currency,
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
        return start_new_bucket(services, block, token, currency, aggregated, interval);
    }

    for (_, id) in previous {
        let aggregated_interval = repo.by_id.get(&id)?;
        if let Some(aggregated_interval) = aggregated_interval {
            if block.median_time - aggregated.block.median_time > interval.clone() as i64 {
                return start_new_bucket(services, block, token, currency, aggregated, interval);
            }

            forward_aggregate(services, (id, &aggregated_interval), aggregated)?;
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
    let repo = &services.oracle_price_aggregated_interval;
    let previous = repo
        .by_key
        .list(
            Some((token.to_string(), currency.to_string(), interval.clone())),
            SortOrder::Descending,
        )?
        .take(1)
        .map(|item| {
            let (_, id) = item?;
            let price = services
                .oracle_price_aggregated_interval
                .by_id
                .get(&id)?
                .context(OtherSnafu {
                    msg: "Missing oracle price aggregated interval index",
                })?;
            Ok((id, price))
        })
        .collect::<Result<Vec<_>>>()?;

    let (prev_id, previous) = &previous[0];

    if previous.aggregated.count == 1 {
        return repo.by_id.delete(prev_id);
    }

    let last_price = previous.aggregated.clone();
    let count = last_price.count - 1;

    let aggregated_amount = backward_aggregate_value(
        last_price.amount,
        aggregated.aggregated.amount,
        Decimal::from(count),
    )?;

    let aggregated_weightage = backward_aggregate_value(
        last_price.weightage,
        aggregated.aggregated.weightage,
        Decimal::from(count),
    )?;

    let aggregated_active = backward_aggregate_value(
        last_price.oracles.active,
        aggregated.aggregated.oracles.active,
        Decimal::from(last_price.count),
    )?;

    let aggregated_total = backward_aggregate_value(
        Decimal::from(last_price.oracles.total),
        Decimal::from(aggregated.aggregated.oracles.total),
        Decimal::from(last_price.count),
    )?;

    let aggregated_interval = OraclePriceAggregatedInterval {
        aggregated: OraclePriceAggregatedIntervalAggregated {
            amount: aggregated_amount,
            weightage: aggregated_weightage,
            count,
            oracles: OraclePriceAggregatedIntervalAggregatedOracles {
                active: aggregated_active,
                total: aggregated_total
                    .to_i32()
                    .context(ToPrimitiveSnafu { msg: "to_i32" })?,
            },
        },
        block: previous.block.clone(),
    };
    repo.by_id.put(prev_id, &aggregated_interval)?;
    repo.by_key.put(
        &(prev_id.0.clone(), prev_id.1.clone(), prev_id.2.clone()),
        prev_id,
    )?;
    Ok(())
}

fn forward_aggregate(
    services: &Arc<Services>,
    previous: (
        OraclePriceAggregatedIntervalId,
        &OraclePriceAggregatedInterval,
    ),
    aggregated: &OraclePriceAggregated,
) -> Result<()> {
    let (prev_id, previous) = previous;
    let last_price = previous.aggregated.clone();
    let count = last_price.count + 1;

    let aggregated_amount = forward_aggregate_value(
        last_price.amount,
        aggregated.aggregated.amount,
        Decimal::from(count),
    )?;

    let aggregated_weightage = forward_aggregate_value(
        last_price.weightage,
        aggregated.aggregated.weightage,
        Decimal::from(count),
    )?;

    let aggregated_active = forward_aggregate_value(
        last_price.oracles.active,
        aggregated.aggregated.oracles.active,
        Decimal::from(last_price.count),
    )?;

    let aggregated_total = forward_aggregate_value(
        Decimal::from(last_price.oracles.total),
        Decimal::from(aggregated.aggregated.oracles.total),
        Decimal::from(last_price.count),
    )?;

    let aggregated_interval = OraclePriceAggregatedInterval {
        aggregated: OraclePriceAggregatedIntervalAggregated {
            amount: aggregated_amount,
            weightage: aggregated_weightage,
            count,
            oracles: OraclePriceAggregatedIntervalAggregatedOracles {
                active: aggregated_active,
                total: aggregated_total
                    .to_i32()
                    .context(ToPrimitiveSnafu { msg: "to_i32" })?,
            },
        },
        block: previous.block.clone(),
    };
    services
        .oracle_price_aggregated_interval
        .by_id
        .put(&prev_id, &aggregated_interval)?;
    services.oracle_price_aggregated_interval.by_key.put(
        &(prev_id.0.clone(), prev_id.1.clone(), prev_id.2.clone()),
        &prev_id,
    )?;
    Ok(())
}

fn forward_aggregate_value(
    last_value: Decimal,
    new_value: Decimal,
    count: Decimal,
) -> Result<Decimal> {
    (last_value * count + new_value)
        .checked_div(count + dec!(1))
        .context(ArithmeticUnderflowSnafu)
}

fn backward_aggregate_value(
    last_value: Decimal,
    new_value: Decimal,
    count: Decimal,
) -> Result<Decimal> {
    (last_value * count - new_value)
        .checked_div(count - dec!(1))
        .context(ArithmeticUnderflowSnafu)
}

fn get_previous_oracle(
    services: &Arc<Services>,
    oracle_id: Txid,
) -> Result<(OracleHistoryId, Oracle)> {
    let previous = services
        .oracle_history
        .by_id
        .list(Some((oracle_id, u32::MAX)), SortOrder::Descending)?
        .next()
        .transpose()?;

    let Some(previous) = previous else {
        return Err(Error::NotFoundIndex {
            action: IndexAction::Index,
            r#type: "OracleHistory".to_string(),
            id: oracle_id.to_string(),
        });
    };

    Ok(previous)
}
