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
        ArithmeticOverflowSnafu, ArithmeticUnderflowSnafu, Error, IndexAction, NotFoundIndexSnafu,
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

        for currency_pair in self.price_feeds.iter().rev() {
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
    fn index(self, services: &Arc<Services>, _ctx: &Context) -> Result<()> {
        let oracle_id = self.oracle_id;
        services.oracle.by_id.delete(&oracle_id)?;

        let (_, mut previous) =
            get_previous_oracle(services, oracle_id)?.context(NotFoundIndexSnafu {
                action: IndexAction::Index,
                r#type: "RemoveOracle".to_string(),
                id: oracle_id.to_string(),
            })?;

        for PriceFeed { token, currency } in previous.price_feeds.drain(..) {
            services
                .oracle_token_currency
                .by_id
                .delete(&(token, currency, oracle_id))?;
        }

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, _ctx: &Context) -> Result<()> {
        trace!("[RemoveOracle] Invalidating...");
        let oracle_id = self.oracle_id;
        let (_, previous) =
            get_previous_oracle(services, oracle_id)?.context(NotFoundIndexSnafu {
                action: IndexAction::Invalidate,
                r#type: "RemoveOracle".to_string(),
                id: oracle_id.to_string(),
            })?;

        let oracle = Oracle {
            owner_address: previous.owner_address,
            weightage: previous.weightage,
            price_feeds: previous.price_feeds.clone(),
            block: previous.block,
        };

        services.oracle.by_id.put(&oracle_id, &oracle)?;

        for price_feed in previous.price_feeds.into_iter().rev() {
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
        let oracle_id = self.oracle_id;
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

        let (_, previous) =
            get_previous_oracle(services, oracle_id)?.context(NotFoundIndexSnafu {
                action: IndexAction::Index,
                r#type: "UpdateOracle".to_string(),
                id: oracle_id.to_string(),
            })?;
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
        let oracle_id = self.oracle_id;
        services
            .oracle_history
            .by_id
            .delete(&(oracle_id, context.block.height))?;

        let price_feeds = self.price_feeds.as_ref();
        for pair in price_feeds.iter().rev() {
            services.oracle_token_currency.by_id.delete(&(
                pair.token.clone(),
                pair.currency.clone(),
                self.oracle_id,
            ))?;
        }
        let ((prev_oracle_id, _), previous) =
            get_previous_oracle(services, oracle_id)?.context(NotFoundIndexSnafu {
                action: IndexAction::Invalidate,
                r#type: "UpdateOracle".to_string(),
                id: oracle_id.to_string(),
            })?;

        let prev_oracle = Oracle {
            owner_address: previous.owner_address,
            weightage: previous.weightage,
            price_feeds: previous.price_feeds.clone(),
            block: previous.block.clone(),
        };
        services.oracle.by_id.put(&(prev_oracle_id), &prev_oracle)?;

        for price_feed in previous.price_feeds.iter().rev() {
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
    pair: &(Token, Currency),
) -> Result<Option<OraclePriceAggregated>> {
    let (token, currency) = pair;
    let oracle_repo = &services.oracle_token_currency;

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
        .take_while(|item| matches!(item, Ok((k, _)) if &k.0 == token && &k.1 == currency))
        .flatten()
        .collect::<Vec<_>>();

    let mut aggregated_total = Decimal::zero();
    let mut aggregated_count = Decimal::zero();
    let mut aggregated_weightage = Decimal::zero();

    let base_id = Txid::from_byte_array([0xffu8; 32]);
    let oracles_len = oracles.len();
    for (id, oracle) in oracles {
        if oracle.weightage == 0 {
            trace!("Skipping oracle with zero weightage: {:?}", oracle);
            continue;
        }

        let feed = services
            .oracle_price_feed
            .by_id
            .list(
                Some((id.0, id.1, id.2, [0xffu8; 4], base_id)),
                SortOrder::Descending,
            )?
            .next()
            .transpose()?;

        let Some((_, feed)) = feed else { continue };

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
            aggregated_total = aggregated_total
                .checked_add(weighted_amount)
                .context(ArithmeticOverflowSnafu)?;
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
            price_aggregated.block.median_time.to_be_bytes(),
            price_aggregated.block.height.to_be_bytes(),
        );
        services.oracle_price_aggregated.by_id.put(&id, &price_aggregated)?;
        let price_repo = &services.price_ticker;
        let id = (
            price_aggregated.aggregated.oracles.total.to_be_bytes(),
            price_aggregated.block.height.to_be_bytes(),
            token.clone(),
            currency.clone(),
        );
        let prev_price = price_repo
            .by_id
            .list(Some(id.clone()), SortOrder::Descending)?
            .find(|item| match item {
                Ok(((_, _, t, c), _)) => t == &token && c == &currency,
                _ => true
            })
            .transpose()?;
        if let Some((k, _)) = prev_price {
            price_repo.by_id.delete(&k)?
        }
        price_repo.by_id.put(
            &id,
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
            context.block.median_time.to_be_bytes(),
            context.block.height.to_be_bytes(),
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
        let mut pairs = HashSet::new();
        let feeds = map_price_feeds(&self, context);
        for (id, feed) in &feeds {
            let token = id.0.clone();
            let currency = id.1.clone();
            pairs.insert((token, currency));
            services.oracle_price_feed.by_id.put(id, feed)?;
        }

        index_set_oracle_data(services, context, &pairs)?;

        index_set_oracle_data_interval(services, context, &pairs)?;

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let oracle_repo = &services.oracle_price_aggregated;

        let feeds = map_price_feeds(self, context);

        for ((token, currency, _, _, _), _) in feeds.iter().rev() {
            let id = (
                token.clone(),
                currency.clone(),
                context.block.median_time.to_be_bytes(),
                context.block.height.to_be_bytes(),
            );

            let aggregated = oracle_repo.by_id.get(&id)?;

            let Some(aggregated) = aggregated else {
                continue;
            };

            for interval in AGGREGATED_INTERVALS.into_iter().rev() {
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
                ctx.block.height.to_be_bytes(),
                ctx.tx.txid,
            );

            let oracle_price_feed = OraclePriceFeed {
                amount: token_amount.amount,
                block: ctx.block.clone(),
                time: data.timestamp as i32,
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
    let id = (token, currency, interval, block.height.to_be_bytes());
    services.oracle_price_aggregated_interval.by_id.put(
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
        .by_id
        .list(
            Some((
                token.clone(),
                currency.clone(),
                interval.clone(),
                [0xffu8; 4],
            )),
            SortOrder::Descending,
        )?
        .take_while(|item| match item {
            Ok(((t, c, i, _), _)) => {
                t == &token.clone() && c == &currency.clone() && i == &interval.clone()
            }
            _ => true,
        })
        .next()
        .transpose()?;

    let Some(previous) = previous else {
        return start_new_bucket(services, block, token, currency, aggregated, interval);
    };

    if block.median_time - aggregated.block.median_time > interval.clone() as i64 {
        return start_new_bucket(services, block, token, currency, aggregated, interval);
    };

    forward_aggregate(services, previous, aggregated)?;

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
        .by_id
        .list(
            Some((
                token.to_string(),
                currency.to_string(),
                interval.clone(),
                [0xffu8; 4],
            )),
            SortOrder::Descending,
        )?
        .next()
        .transpose()?;

    let Some((prev_id, previous)) = previous else {
        return Err(Error::NotFoundIndex {
            action: IndexAction::Invalidate,
            r#type: "Invalidate oracle price aggregated interval".to_string(),
            id: format!("{}-{}-{:?}", token, currency, interval),
        });
    };

    if previous.aggregated.count == 1 {
        return repo.by_id.delete(&prev_id);
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
    repo.by_id.put(&prev_id, &aggregated_interval)?;
    Ok(())
}

fn forward_aggregate(
    services: &Arc<Services>,
    previous: (
        OraclePriceAggregatedIntervalId,
        OraclePriceAggregatedInterval,
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
) -> Result<Option<(OracleHistoryId, Oracle)>> {
    let previous = services
        .oracle_history
        .by_id
        .list(Some((oracle_id, u32::MAX)), SortOrder::Descending)?
        .next()
        .transpose()?;

    Ok(previous)
}
