use std::collections::HashSet;

use dftx_rs::{common::CompactVec, oracles::*, Transaction};
use hyper::service::Service;

use super::BlockContext;
use crate::{
    indexer::{Index, Result},
    model::{
        OraclePriceAggregated, OraclePriceAggregatedAggregated,
        OraclePriceAggregatedAggregatedOracles, OraclePriceFeed,
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

        todo!()
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        todo!()
    }
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
                    id: format!(
                        "{}-{}-{}-{}",
                        token_price.token,
                        token_amount.currency,
                        set_data.oracle_id,
                        tx.txid()
                    ),
                    key: format!(
                        "{}-{}-{}",
                        token_price.token, token_amount.currency, set_data.oracle_id
                    ),
                    sort: hex::encode(ctx.height.to_string() + &tx.txid().to_string()),
                    amount: token_amount.amount.to_string(),
                    currency: token_amount.currency.clone(),
                    block: BlockContext {
                        hash: ctx.hash.clone(),
                        height: ctx.height,
                        median_time: ctx.median_time,
                        time: ctx.time,
                    },
                    oracle_id: set_data.oracle_id.to_string(),
                    time: set_data.timestamp as i32,
                    token: token_price.token.clone(),
                    txid: tx.txid().to_string(),
                };

                result.push(oracle_price_feed);
            }
        }
    }

    Ok(result)
}
