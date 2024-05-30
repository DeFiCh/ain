use std::{collections::HashMap, str::FromStr, sync::Arc};

use anyhow::{format_err, Context};
use defichain_rpc::{json::poolpair::PoolPairInfo, BlockchainRPC};
use rust_decimal::{prelude::FromPrimitive, Decimal};
use rust_decimal_macros::dec;
use serde::Serialize;

use super::{AppContext, PoolPairAprResponse};
use crate::{
    api::{
        cache::{get_gov_cached, get_pool_pair_cached, get_token_cached},
        pool_pair::path::{get_best_path, BestSwapPathResponse},
    },
    error::{Error, NotFoundKind},
    indexer::PoolSwapAggregatedInterval,
    model::PoolSwapAggregatedAggregated,
    repository::{RepositoryOps, SecondaryIndex},
    storage::SortOrder,
    Result,
};

#[derive(Serialize, Debug, Clone, Default)]
pub struct PoolPairVolumeResponse {
    pub d30: Decimal,
    pub h24: Decimal,
}

async fn get_usd_per_dfi(ctx: &Arc<AppContext>) -> Result<Decimal> {
    let usdt = get_pool_pair_cached(ctx, "USDT-DFI".to_string()).await?;

    let usdc = get_pool_pair_cached(ctx, "USDC-DFI".to_string()).await?;

    let mut total_usd = dec!(0);
    let mut total_dfi = dec!(0);

    fn add(
        p: PoolPairInfo,
        mut total_usd: Decimal,
        mut total_dfi: Decimal,
    ) -> Result<(Decimal, Decimal)> {
        let reserve_a = Decimal::from_f64(p.reserve_a).unwrap_or_default();
        let reserve_b = Decimal::from_f64(p.reserve_b).unwrap_or_default();
        if p.id_token_a == "0" {
            total_usd = total_usd
                .checked_add(reserve_b)
                .ok_or_else(|| Error::OverflowError)?;
            total_dfi = total_dfi
                .checked_add(reserve_a)
                .ok_or_else(|| Error::OverflowError)?;
        } else if p.id_token_b == "0" {
            total_usd = total_usd
                .checked_add(reserve_a)
                .ok_or_else(|| Error::OverflowError)?;
            total_dfi = total_dfi
                .checked_add(reserve_b)
                .ok_or_else(|| Error::OverflowError)?;
        }
        Ok((total_usd, total_dfi))
    }

    if let Some((_, usdt)) = usdt {
        (total_usd, total_dfi) = add(usdt, total_usd, total_dfi)?;
    };

    if let Some((_, usdc)) = usdc {
        (total_usd, total_dfi) = add(usdc, total_usd, total_dfi)?;
    };

    if !total_usd.is_zero() {
        let res = total_usd
            .checked_div(total_dfi)
            .ok_or_else(|| Error::UnderflowError)?;
        return Ok(res);
    };

    Ok(dec!(0))
}

async fn get_total_liquidity_usd_by_best_path(
    ctx: &Arc<AppContext>,
    p: &PoolPairInfo,
) -> Result<Decimal> {
    let usdt = get_token_cached(ctx, "USDT").await?;
    if usdt.is_none() {
        return Ok(dec!(0));
    };

    let (usdt_id, _) = usdt.unwrap();

    let mut a_token_rate = dec!(1);
    let mut b_token_rate = dec!(1);

    if p.id_token_a != usdt_id {
        let BestSwapPathResponse {
            estimated_return, ..
        } = get_best_path(ctx, &p.id_token_a, &usdt_id).await?;
        a_token_rate = Decimal::from_str(estimated_return.as_str())?;
    }

    if p.id_token_a != usdt_id {
        let BestSwapPathResponse {
            estimated_return, ..
        } = get_best_path(ctx, &p.id_token_b, &usdt_id).await?;
        b_token_rate = Decimal::from_str(estimated_return.as_str())?;
    }

    let reserve_a = Decimal::from_f64(p.reserve_a).unwrap_or_default();
    let reserve_b = Decimal::from_f64(p.reserve_b).unwrap_or_default();

    let a = a_token_rate
        .checked_mul(reserve_a)
        .ok_or_else(|| Error::OverflowError)?;

    let b = b_token_rate
        .checked_mul(reserve_b)
        .ok_or_else(|| Error::OverflowError)?;

    let res = a.checked_add(b).ok_or_else(|| Error::OverflowError)?;

    Ok(res)
}

pub async fn get_total_liquidity_usd(ctx: &Arc<AppContext>, p: &PoolPairInfo) -> Result<Decimal> {
    let parts = p.symbol.split('-').collect::<Vec<&str>>();
    let [a, b] = <[&str; 2]>::try_from(parts)
        .map_err(|_| format_err!("Invalid pool pair symbol structure"))?;

    let reserve_a = Decimal::from_f64(p.reserve_a).unwrap_or_default();
    let reserve_b = Decimal::from_f64(p.reserve_b).unwrap_or_default();

    if ["DUSD", "USDT", "USDC"].contains(&a) {
        return reserve_a
            .checked_mul(dec!(2))
            .ok_or_else(|| Error::OverflowError);
    };

    if ["DUSD", "USDT", "USDC"].contains(&b) {
        return reserve_b
            .checked_mul(dec!(2))
            .ok_or_else(|| Error::OverflowError);
    };

    let usdt_per_dfi = get_usd_per_dfi(ctx).await?;
    if usdt_per_dfi.is_zero() {
        return Ok(usdt_per_dfi);
    };

    if a == "DFI" {
        return reserve_a
            .checked_mul(dec!(2))
            .ok_or_else(|| Error::OverflowError)?
            .checked_mul(usdt_per_dfi)
            .ok_or_else(|| Error::OverflowError);
    };

    if b == "DFI" {
        return reserve_b
            .checked_mul(dec!(2))
            .ok_or_else(|| Error::OverflowError)?
            .checked_mul(usdt_per_dfi)
            .ok_or_else(|| Error::OverflowError);
    };

    let res = get_total_liquidity_usd_by_best_path(ctx, p).await?;
    Ok(res)
}

fn calculate_rewards(accounts: &[String], dfi_price_usdt: Decimal) -> Result<Decimal> {
    let rewards = accounts.iter().try_fold(dec!(0), |accumulate, account| {
        let parts = account.split('-').collect::<Vec<&str>>();
        let [amount, token] = parts
            .as_slice()
            .try_into()
            .context("Invalid pool pair symbol structure")?;

        if token != "0" && token != "DFI" {
            return Ok(accumulate);
        }

        let yearly = Decimal::from_str(amount)
            .context("convert numeric string to number")?
            .checked_mul(dec!(2880))
            .and_then(|v| v.checked_mul(dec!(365)))
            .and_then(|v| v.checked_mul(dfi_price_usdt))
            .context("yearly reward overflow")?;
        accumulate
            .checked_add(yearly)
            .context("accumlate reward overflow")
    })?;
    Ok(rewards)
}

async fn get_yearly_custom_reward_usd(ctx: &Arc<AppContext>, p: &PoolPairInfo) -> Result<Decimal> {
    if p.custom_rewards.is_none() {
        return Ok(dec!(0));
    };

    let dfi_price_usdt = get_usd_per_dfi(ctx).await?;
    if dfi_price_usdt.is_zero() {
        return Ok(dfi_price_usdt);
    };

    p.custom_rewards.as_ref().map_or(Ok(dec!(0)), |rewards| {
        calculate_rewards(rewards, dfi_price_usdt)
    })
}

async fn get_daily_dfi_reward(ctx: &Arc<AppContext>) -> Result<Decimal> {
    let gov = get_gov_cached(ctx, "LP_DAILY_DFI_REWARD".to_string()).await?;

    let reward = gov
        .get("LP_DAILY_DFI_REWARD")
        .and_then(|v| v.as_f64()) // eg: { "LP_DAILY_DFI_REWARD": 3664.80000000 }
        .unwrap_or_default();

    let daily_dfi_reward =
        Decimal::from_f64(reward).ok_or_else(|| Error::DecimalConversionError)?;
    Ok(daily_dfi_reward)
}

async fn get_loan_token_splits(ctx: &Arc<AppContext>) -> Result<Option<serde_json::Value>> {
    let splits = get_gov_cached(ctx, "LP_LOAN_TOKEN_SPLITS".to_string())
        .await?
        .get("LP_LOAN_TOKEN_SPLITS")
        .cloned();
    Ok(splits)
}

async fn get_yearly_reward_pct_usd(ctx: &Arc<AppContext>, p: &PoolPairInfo) -> Result<Decimal> {
    // if p.reward_pct.is_none() {
    //   return dec!(0)
    // };

    let dfi_price_usd = get_usd_per_dfi(ctx).await?;
    let daily_dfi_reward = get_daily_dfi_reward(ctx).await?;

    let reward_pct = Decimal::from_f64(p.reward_pct).unwrap_or_default();
    reward_pct
        .checked_mul(daily_dfi_reward)
        .ok_or_else(|| Error::OverflowError)?
        .checked_mul(dec!(365))
        .ok_or_else(|| Error::OverflowError)?
        .checked_mul(dfi_price_usd)
        .ok_or_else(|| Error::OverflowError)
}

async fn get_block_subsidy(eunos_height: u32, height: u32) -> Result<Decimal> {
    let eunos_height =
        Decimal::from_u32(eunos_height).ok_or_else(|| Error::DecimalConversionError)?;
    let height = Decimal::from_u32(height).ok_or_else(|| Error::DecimalConversionError)?;
    let mut block_subsidy = dec!(405.04);

    if height >= eunos_height {
        let reduction_amount = dec!(0.01658); // 1.658%
        let mut reductions = height
            .checked_sub(eunos_height)
            .ok_or_else(|| Error::UnderflowError)?
            .checked_div(dec!(32690))
            .ok_or_else(|| Error::UnderflowError)?
            .floor();

        while reductions >= dec!(0) {
            let amount = reduction_amount
                .checked_mul(block_subsidy)
                .ok_or_else(|| Error::OverflowError)?;
            if amount <= dec!(0.00001) {
                return Ok(dec!(0));
            };
            block_subsidy = block_subsidy
                .checked_sub(amount)
                .ok_or_else(|| Error::UnderflowError)?;
            reductions = reductions
                .checked_sub(dec!(1))
                .ok_or_else(|| Error::UnderflowError)?;
        }
    };

    Ok(block_subsidy)
}

// TODO(): cached
async fn get_loan_emission(ctx: &Arc<AppContext>) -> Result<Decimal> {
    let info = ctx.client.get_blockchain_info().await?;
    let eunos_height = info
        .softforks
        .get("eunos")
        .and_then(|eunos| eunos.height)
        .ok_or_else(|| format_err!("BlockchainInfo eunos height field is missing"))?;

    get_block_subsidy(eunos_height, info.blocks).await
}

async fn get_yearly_reward_loan_usd(ctx: &Arc<AppContext>, id: &String) -> Result<Decimal> {
    let splits = get_loan_token_splits(ctx).await?;
    let value = splits.unwrap_or_default();
    let split = value
        .as_object()
        .and_then(|obj| obj.get(id))
        .and_then(|v| v.as_f64())
        .unwrap_or_default();
    let split = Decimal::from_f64(split).ok_or_else(|| Error::DecimalConversionError)?;

    let dfi_price_usd = get_usd_per_dfi(ctx).await?;

    let loan_emission = get_loan_emission(ctx).await?;

    loan_emission
        .checked_mul(split) // 60 * 60 * 24 / 30, 30 seconds = 1 block
        .ok_or_else(|| Error::OverflowError)?
        .checked_mul(dec!(2880)) // 60 * 60 * 24 / 30, 30 seconds = 1 block
        .ok_or_else(|| Error::OverflowError)?
        .checked_mul(dec!(365)) // 60 * 60 * 24 / 30, 30 seconds = 1 block
        .ok_or_else(|| Error::OverflowError)?
        .checked_mul(dfi_price_usd) // 60 * 60 * 24 / 30, 30 seconds = 1 block
        .ok_or_else(|| Error::OverflowError)
}

async fn gather_amount(ctx: &Arc<AppContext>, pool_id: u32, interval: u32, count: usize) -> Result<Decimal> {
    let repository = &ctx.services.pool_swap_aggregated;

    let swaps = repository
        .by_key
        .list(Some((pool_id, interval, i64::MAX)), SortOrder::Descending)?
        .take(count)
        .take_while(|item| match item {
            Ok((k, _)) => k.0 == pool_id && k.1 == interval,
            _ => true,
        })
        .map(|e| repository.by_key.retrieve_primary_value(e))
        .collect::<Result<Vec<_>>>()?;

    let mut aggregated = HashMap::<String, Decimal>::new();

    for swap in swaps {
        let token_ids = swap.aggregated.amounts.keys();
        for token_id in token_ids {
            let from_amount = swap
                .aggregated
                .amounts
                .get(token_id)
                .map(|amt| Decimal::from_str(amt))
                .transpose()?
                .unwrap_or(dec!(0));

            let amount = if let Some(amount) = aggregated.get(token_id) {
                amount.checked_add(from_amount).ok_or(Error::OverflowError)?
            } else {
                from_amount
            };

            aggregated.insert(token_id.to_string(), amount);
        }
    }

    let mut volume = dec!(0);

    for token_id in aggregated.keys() {
        let token_price = get_token_usd_value(ctx, token_id).await?;
        let amount = aggregated
            .get(token_id)
            .cloned()
            .unwrap_or(dec!(0));
        volume = volume
            .checked_add(
                token_price
                .checked_mul(amount)
                .ok_or(Error::OverflowError)?
            )
            .ok_or(Error::OverflowError)?;
    }

   Ok(volume)
}

pub async fn get_usd_volume(ctx: &Arc<AppContext>, id: &str) -> Result<PoolPairVolumeResponse> {
    let pool_id = id.parse::<u32>()?;
    Ok(PoolPairVolumeResponse {
        h24: gather_amount(ctx, pool_id, PoolSwapAggregatedInterval::OneHour as u32, 24).await?,
        d30: gather_amount(ctx, pool_id, PoolSwapAggregatedInterval::OneDay as u32, 30).await?,
    })
}

/// Estimate yearly commission rate by taking 24 hour commission x 365 days
async fn get_yearly_commission_estimate(ctx: &Arc<AppContext>, id: &str, p: &PoolPairInfo) -> Result<Decimal> {
    let volume = get_usd_volume(ctx, id).await?;
    let commission = Decimal::from_f64(p.commission).unwrap_or_default();
    commission
        .checked_mul(volume.h24)
        .ok_or_else(|| Error::OverflowError)?
        .checked_mul(dec!(365))
        .ok_or_else(|| Error::OverflowError)
}

pub async fn get_apr(
    ctx: &Arc<AppContext>,
    id: &String,
    p: &PoolPairInfo,
) -> Result<PoolPairAprResponse> {
    let custom_usd = get_yearly_custom_reward_usd(ctx, p).await?; // 0
    let pct_usd = get_yearly_reward_pct_usd(ctx, p).await?;
    let loan_usd = get_yearly_reward_loan_usd(ctx, id).await?;
    let total_liquidity_usd = get_total_liquidity_usd(ctx, p).await?;

    let yearly_usd = custom_usd
        .checked_add(pct_usd)
        .ok_or_else(|| Error::OverflowError)?
        .checked_add(loan_usd)
        .ok_or_else(|| Error::OverflowError)?;

    if yearly_usd.is_zero() {
        return Ok(PoolPairAprResponse::default())
    };

    // 1 == 100%, 0.1 = 10%
    let reward = yearly_usd
        .checked_div(total_liquidity_usd)
        .ok_or_else(|| Error::UnderflowError)?;

    let yearly_commission = get_yearly_commission_estimate(ctx, id, p).await?;
    let commission = yearly_commission
        .checked_div(total_liquidity_usd)
        .ok_or_else(|| Error::UnderflowError)?;

    let total = reward
        .checked_add(commission)
        .ok_or_else(|| Error::OverflowError)?;

    Ok(PoolPairAprResponse {
        reward,
        commission,
        total,
    })
}

async fn get_pool_pair(ctx: &Arc<AppContext>, a: &str, b: &str) -> Result<Option<PoolPairInfo>> {
    let ab = get_pool_pair_cached(ctx, format!("{a}-{b}")).await?;
    if let Some((_, info)) = ab {
        Ok(Some(info))
    } else {
        let ba = get_pool_pair_cached(ctx, format!("{b}-{a}")).await?;
        if let Some((_, info)) = ba {
            Ok(Some(info))
        } else {
            Ok(None)
        }
    }
}

async fn get_token_usd_value(ctx: &Arc<AppContext>, token_id: &str) -> Result<Decimal> {
    let (_, info) = get_token_cached(ctx, token_id)
        .await?
        .ok_or(Error::NotFound(NotFoundKind::Token))?;

    if ["DUSD", "USDT", "USDC"].contains(&info.symbol.as_str()) {
        return Ok(dec!(1));
    };

    let dusd_pool = get_pool_pair(ctx, &info.symbol, "DUSD").await?;
    if let Some(p) = dusd_pool {
        let parts = p.symbol.split('-').collect::<Vec<&str>>();
        let [a, _] = <[&str; 2]>::try_from(parts)
            .map_err(|_| format_err!("Invalid pool pair symbol structure"))?;
        let reserve_a = Decimal::from_f64(p.reserve_a).ok_or(Error::DecimalConversionError)?;
        let reserve_b = Decimal::from_f64(p.reserve_b).ok_or(Error::DecimalConversionError)?;
        if a == "DUSD" {
            return reserve_a
                .checked_div(reserve_b)
                .ok_or(Error::UnderflowError);
        };
        return reserve_b
            .checked_div(reserve_a)
            .ok_or(Error::UnderflowError);
    }

    let dfi_pool = get_pool_pair(ctx, &info.symbol, "DFI").await?;
    if let Some(p) = dfi_pool {
        let usd_per_dfi = get_usd_per_dfi(ctx).await?;
        let reserve_a = Decimal::from_f64(p.reserve_a).ok_or(Error::DecimalConversionError)?;
        let reserve_b = Decimal::from_f64(p.reserve_b).ok_or(Error::DecimalConversionError)?;
        if p.id_token_a == *"0" {
            return reserve_a
                .checked_div(reserve_b)
                .ok_or(Error::UnderflowError)?
                .checked_mul(usd_per_dfi)
                .ok_or(Error::OverflowError);
        }
        return reserve_b
            .checked_div(reserve_a)
            .ok_or(Error::UnderflowError)?
            .checked_mul(usd_per_dfi)
            .ok_or(Error::OverflowError);
    }

    Ok(dec!(0))
}

pub async fn get_aggregated_in_usd(
    ctx: &Arc<AppContext>,
    aggregated: &PoolSwapAggregatedAggregated,
) -> Result<Decimal> {
    let mut value = dec!(0);

    for (token_id, amount) in &aggregated.amounts {
        let token_price = get_token_usd_value(ctx, token_id).await?;
        let amount = Decimal::from_str(amount)?;
        value = value
            .checked_add(token_price)
            .ok_or(Error::OverflowError)?
            .checked_mul(amount)
            .ok_or(Error::OverflowError)?
    }

    Ok(value)
}
