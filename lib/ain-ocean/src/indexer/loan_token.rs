use std::sync::Arc;

use ain_dftx::loans::SetLoanToken;
use rust_decimal::{
    prelude::{FromPrimitive, Zero},
    Decimal,
};

use crate::{
    indexer::{Context, Index, Result},
    model::{
        OraclePriceActive, OraclePriceActiveActive, OraclePriceActiveActiveOracles,
        OraclePriceActiveNext, OraclePriceActiveNextOracles, OraclePriceAggregated,
    },
    repository::RepositoryOps,
    storage::SortOrder,
    Services,
};
impl Index for SetLoanToken {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        let ticker_id = (self.currency_pair.token, self.currency_pair.currency);
        let aggregated_price = services
            .oracle_price_aggregated
            .by_key
            .list(Some(ticker_id.clone()), SortOrder::Descending)?
            .map(|item| {
                let (_, id) = item?;
                let b = services
                    .oracle_price_aggregated
                    .by_id
                    .get(&id)?
                    .ok_or("Missing oracle previous history index")?;

                Ok(b)
            })
            .collect::<std::result::Result<Vec<_>, Box<dyn std::error::Error>>>()?;

        if !aggregated_price.is_empty() {
            let previous_price = services
                .oracle_price_active
                .by_key
                .list(Some(ticker_id.clone()), SortOrder::Descending)?
                .map(|item| {
                    let (_, id) = item?;
                    let b = services
                        .oracle_price_active
                        .by_id
                        .get(&id)?
                        .ok_or("Missing oracle previous history index")?;

                    Ok(b)
                })
                .collect::<std::result::Result<Vec<_>, Box<dyn std::error::Error>>>()?;
            let price_active_id = (
                ticker_id.0.clone(),
                ticker_id.1.clone(),
                aggregated_price[0].block.height,
            );

            let oracle_price_key = (ticker_id.0, ticker_id.1);
            let next_price = match aggregated_validate(aggregated_price[0].clone(), ctx) {
                true => OraclePriceActiveNext {
                    amount: aggregated_price[0].aggregated.amount.clone(),
                    weightage: aggregated_price[0].aggregated.weightage,
                    oracles: OraclePriceActiveNextOracles {
                        active: aggregated_price[0].aggregated.oracles.active,
                        total: aggregated_price[0].aggregated.oracles.total,
                    },
                },
                false => Default::default(),
            };

            let active_price: OraclePriceActiveActive;

            if previous_price.is_empty() {
                active_price = OraclePriceActiveActive {
                    amount: Default::default(),
                    weightage: Default::default(),
                    oracles: OraclePriceActiveActiveOracles {
                        active: Default::default(),
                        total: Default::default(),
                    },
                };
            } else {
                if let Some(next) = previous_price.get(0).map(|price| &price.next) {
                    active_price = OraclePriceActiveActive {
                        amount: next.amount.clone(),
                        weightage: next.weightage,
                        oracles: OraclePriceActiveActiveOracles {
                            active: next.oracles.active,
                            total: next.oracles.total,
                        },
                    };
                } else {
                    let oracles = OraclePriceActiveActiveOracles {
                        active: previous_price[0].active.oracles.active,
                        total: previous_price[0].active.oracles.total,
                    };
                    active_price = OraclePriceActiveActive {
                        amount: previous_price[0].active.amount.clone(),
                        weightage: previous_price[0].active.weightage,
                        oracles: oracles,
                    };
                };
            };
            let oracle_price_active = OraclePriceActive {
                id: price_active_id.clone(),
                key: oracle_price_key,
                sort: hex::encode(ctx.block.height.to_be_bytes()),
                active: active_price.clone(),
                next: next_price.clone(),
                is_live: is_live(active_price, next_price),
                block: ctx.block.clone(),
            };
            services
                .oracle_price_active
                .by_id
                .put(&price_active_id, &oracle_price_active)?;
            services
                .oracle_price_active
                .by_key
                .put(&oracle_price_active.key, &oracle_price_active.id)?;
        }

        Ok(())
    }
    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        let ticker_id = (
            self.currency_pair.token.clone(),
            self.currency_pair.currency.clone(),
            context.block.height,
        );
        services.oracle_price_active.by_id.delete(&ticker_id)?;
        services
            .oracle_price_active
            .by_key
            .delete(&(ticker_id.0, ticker_id.1))?;
        Ok(())
    }
}
pub fn aggregated_validate(aggrigated_price: OraclePriceAggregated, context: &Context) -> bool {
    let minimum_live_oracles: i32 = 2;
    if (aggrigated_price.block.time - context.block.time).abs() >= 3600 {
        return false;
    }
    if aggrigated_price.aggregated.oracles.active < minimum_live_oracles {
        return false;
    }

    if aggrigated_price.aggregated.weightage <= 0 {
        return false;
    }

    true
}

pub fn is_live(active: OraclePriceActiveActive, next: OraclePriceActiveNext) -> bool {
    let deviation_threshold = Decimal::from_f64(0.5).unwrap_or_default();
    let active_price = Decimal::from_str_exact(&active.amount).unwrap_or_default();
    let next_price = Decimal::from_str_exact(&next.amount).unwrap_or_default();
    if active_price > Decimal::zero() && next_price > Decimal::zero() {
        return false;
    }
    let diff = next_price - active_price;
    let abs_diff = diff.abs();
    let threshold = active_price * deviation_threshold;
    if !abs_diff.lt(&threshold) {
        return false;
    }
    true
}
