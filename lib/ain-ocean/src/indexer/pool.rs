use std::{str::FromStr, sync::Arc};

use ain_dftx::pool::*;
use anyhow::format_err;
// use bitcoin::Address;
use log::debug;
use rust_decimal::Decimal;
use rust_decimal_macros::dec;

use super::Context;
use crate::{
    indexer::{tx_result, Index, Result},
    model::{self, PoolSwapAggregatedId, PoolSwapAggregatedKey, PoolSwapResult, TxResult},
    repository::RepositoryOps,
    Error, Services,
};

#[derive(Debug, Clone, Copy)]
pub enum PoolSwapAggregatedInterval {
    OneDay = 86400, // 60 * 60 * 24,
    OneHour = 120,  // 60 * 60,
    Unknown,
}

impl From<u32> for PoolSwapAggregatedInterval {
    fn from(value: u32) -> Self {
        match value {
            86400 => Self::OneDay,
            120 => Self::OneHour,
            _ => Self::Unknown,
        }
    }
}

impl Index for PoolSwap {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        debug!("[Poolswap] Indexing...");
        let txid = ctx.tx.txid;
        let idx = ctx.tx_idx;
        let Some(TxResult::PoolSwap(PoolSwapResult { to_amount, pool_id })) =
            services.result.get(&txid)?
        else {
            // TODO fallback through getaccounthistory when indexing against non-oceanarchive node
            // let address = Address::from_script(&self.to_script, bitcoin::Network::Bitcoin)
            //     .map_err(|e| format_err!("Error getting address from script: {e}"));
            debug!("Missing swap result for {}", ctx.tx.txid.to_string());
            return Err("Missing swap result".into());
        };

        let from = self.from_script;
        let to = self.to_script;
        let from_token_id = self.from_token_id.0;
        let from_amount = self.from_amount;

        let swap = model::PoolSwap {
            id: format!("{}-{}", pool_id, txid),
            // TODO: use hex::encode eg: sort: hex::encode(ctx.block.height + idx)
            sort: format!("{}-{}", ctx.block.height, idx),
            txid,
            txno: idx,
            from_amount,
            from_token_id,
            to_token_id: self.to_token_id.0,
            to_amount,
            pool_id,
            from,
            to,
            block: ctx.block.clone(),
        };
        debug!("swap : {:?}", swap);

        let _ = services
            .pool
            .by_id
            .put(&(pool_id, ctx.block.height, idx), &swap);

        // one day
        {
            let interval_one_day = PoolSwapAggregatedInterval::OneDay as u32;
            let key: PoolSwapAggregatedKey = (pool_id, interval_one_day);
            let encoded_ids = services.pool_swap_aggregated.one_day_by_key.get(&key)?;

            if let Some(encoded_ids) = encoded_ids {
                let decoded_ids = hex::decode(encoded_ids)?;
                if decoded_ids.is_empty() {
                    log::error!(
                        "index swap {txid}: Unable to find {pool_id}-{interval_one_day} for Aggregate Indexing"
                    );
                };

                let deserialized_ids =
                    bincode::deserialize::<Vec<PoolSwapAggregatedId>>(&decoded_ids)?;
                let latest_id = deserialized_ids
                    .last()
                    .ok_or(format_err!("Empty pool swap aggregated ids one day"))?;
                let aggregate = services.pool_swap_aggregated.one_day_by_id.get(latest_id)?;

                if let Some(mut aggregate) = aggregate {
                    let amount = aggregate
                        .aggregated
                        .amounts
                        .get(&from_token_id.to_string())
                        .map(|amt| Decimal::from_str(amt))
                        .transpose()?
                        .unwrap_or(dec!(0));

                    let aggregate_amount = amount
                        .checked_add(Decimal::from(from_amount))
                        .ok_or(Error::OverflowError)?;

                    aggregate
                        .aggregated
                        .amounts
                        .insert(from_token_id.to_string(), aggregate_amount.to_string());

                    services
                        .pool_swap_aggregated
                        .one_day_by_id
                        .put(latest_id, &aggregate)?;
                }
            }
        }

        // one hour
        {
            let interval_one_hour = PoolSwapAggregatedInterval::OneHour as u32;
            let key: PoolSwapAggregatedKey = (pool_id, interval_one_hour);
            let encoded_ids = services.pool_swap_aggregated.one_hour_by_key.get(&key)?;

            if let Some(encoded_ids) = encoded_ids {
                let decoded_ids = hex::decode(encoded_ids)?;
                if decoded_ids.is_empty() {
                    log::error!(
                        "index swap {txid}: Unable to find {pool_id}-{interval_one_hour} for Aggregate Indexing"
                    );
                };

                let deserialized_ids =
                    bincode::deserialize::<Vec<PoolSwapAggregatedId>>(&decoded_ids)?;
                let latest_id = deserialized_ids
                    .last()
                    .ok_or(format_err!("Empty pool swap aggregated ids one hour"))?;
                let aggregate = services
                    .pool_swap_aggregated
                    .one_hour_by_id
                    .get(latest_id)?;

                if let Some(mut aggregate) = aggregate {
                    let amount = aggregate
                        .aggregated
                        .amounts
                        .get(&from_token_id.to_string())
                        .map(|amt| Decimal::from_str(amt))
                        .transpose()?
                        .unwrap_or(dec!(0));

                    let aggregate_amount = amount
                        .checked_add(Decimal::from(from_amount))
                        .ok_or(Error::OverflowError)?;

                    aggregate
                        .aggregated
                        .amounts
                        .insert(from_token_id.to_string(), aggregate_amount.to_string());

                    services
                        .pool_swap_aggregated
                        .one_hour_by_id
                        .put(latest_id, &aggregate)?;
                }
            }
        }

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        let txid = ctx.tx.txid;
        let Some(TxResult::PoolSwap(PoolSwapResult { pool_id, .. })) =
            services.result.get(&txid)?
        else {
            return Err("Missing swap result".into());
        };

        services
            .pool
            .by_id
            .delete(&(pool_id, ctx.block.height, ctx.tx_idx))?;
        tx_result::invalidate(services, &txid)
    }
}

impl Index for CompositeSwap {
    fn index(self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        debug!("[CompositeSwap] Indexing...");
        let txid = ctx.tx.txid;
        let Some(TxResult::PoolSwap(PoolSwapResult { to_amount, .. })) =
            services.result.get(&txid)?
        else {
            // TODO fallback through getaccounthistory when indexing against non-oceanarchive node
            // let address =
            //     Address::from_script(&self.pool_swap.to_script, bitcoin::Network::Bitcoin)
            //         .map_err(|e| format_err!("Error getting address from script: {e}"));
            debug!("Missing swap result for {}", txid.to_string());
            return Err("Missing swap result".into());
        };

        let from = self.pool_swap.from_script;
        let to = self.pool_swap.to_script;
        for pool in self.pools.as_ref() {
            let pool_id = pool.id.0 as u32;
            let swap = model::PoolSwap {
                id: format!("{}-{}", pool_id, txid),
                sort: format!("{}-{}", ctx.block.height, ctx.tx_idx),
                txid,
                txno: ctx.tx_idx,
                from_amount: self.pool_swap.from_amount,
                from_token_id: self.pool_swap.from_token_id.0,
                to_token_id: self.pool_swap.to_token_id.0,
                to_amount,
                pool_id,
                from: from.clone(),
                to: to.clone(),
                block: ctx.block.clone(),
            };
            services
                .pool
                .by_id
                .put(&(pool_id, ctx.block.height, ctx.tx_idx), &swap)?;
        }

        Ok(())
    }

    fn invalidate(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        for pool in self.pools.as_ref() {
            let pool_id = pool.id.0 as u32;
            services
                .pool
                .by_id
                .delete(&(pool_id, ctx.block.height, ctx.tx_idx))?;
        }
        tx_result::invalidate(services, &ctx.tx.txid)
    }
}
