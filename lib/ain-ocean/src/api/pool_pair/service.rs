use std::{collections::HashMap, str::FromStr, sync::Arc};

use ain_dftx::{deserialize, pool::CompositeSwap, DfTx, Stack};
use bitcoin::Txid;
use defichain_rpc::{json::poolpair::PoolPairInfo, BlockchainRPC};
use rust_decimal::{prelude::FromPrimitive, Decimal};
use rust_decimal_macros::dec;
use serde::{Deserialize, Serialize};
use snafu::OptionExt;

use super::{AppContext, PoolPairAprResponse};
use crate::{
    api::{
        cache::{get_gov_cached, get_pool_pair_cached, get_token_cached},
        common::{from_script, parse_amount, parse_display_symbol, parse_pool_pair_symbol},
        pool_pair::path::{get_best_path, BestSwapPathResponse},
    },
    error::{
        ArithmeticOverflowSnafu, ArithmeticUnderflowSnafu, DecimalConversionSnafu, Error,
        NotFoundKind, OtherSnafu,
    },
    indexer::PoolSwapAggregatedInterval,
    model::{PoolSwap, PoolSwapAggregatedAggregated},
    storage::{RepositoryOps, SecondaryIndex, SortOrder},
    Result,
};

#[allow(clippy::upper_case_acronyms)]
#[derive(Serialize, Deserialize, Debug, Clone)]
pub enum SwapType {
    BUY,
    SELL,
}

#[derive(Serialize, Debug, Clone, Default)]
pub struct PoolPairVolumeResponse {
    pub d30: Decimal,
    pub h24: Decimal,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapFromToData {
    pub address: String,
    pub amount: String,
    pub symbol: String,
    pub display_symbol: String,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "camelCase")]
pub struct PoolSwapFromTo {
    pub from: Option<PoolSwapFromToData>,
    pub to: Option<PoolSwapFromToData>,
}

pub async fn get_usd_per_dfi(ctx: &Arc<AppContext>) -> Result<Decimal> {
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
                .context(ArithmeticOverflowSnafu)?;
            total_dfi = total_dfi
                .checked_add(reserve_a)
                .context(ArithmeticOverflowSnafu)?;
        } else if p.id_token_b == "0" {
            total_usd = total_usd
                .checked_add(reserve_a)
                .context(ArithmeticOverflowSnafu)?;
            total_dfi = total_dfi
                .checked_add(reserve_b)
                .context(ArithmeticOverflowSnafu)?;
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
            .context(ArithmeticUnderflowSnafu)?;
        return Ok(res);
    };

    Ok(dec!(0))
}

async fn get_total_liquidity_usd_by_best_path(
    ctx: &Arc<AppContext>,
    p: &PoolPairInfo,
) -> Result<Decimal> {
    let token = ain_cpp_imports::get_dst_token("USDT".to_string());
    if token.is_null() {
        return Ok(dec!(0));
    }
    let usdt_id = token.id.to_string();

    let mut a_token_rate = dec!(1);
    let mut b_token_rate = dec!(1);

    if p.id_token_a != usdt_id {
        let BestSwapPathResponse {
            estimated_return, ..
        } = get_best_path(ctx, &p.id_token_a, &usdt_id).await?;
        a_token_rate = estimated_return;
    }

    if p.id_token_a != usdt_id {
        let BestSwapPathResponse {
            estimated_return, ..
        } = get_best_path(ctx, &p.id_token_b, &usdt_id).await?;
        b_token_rate = estimated_return;
    }

    let reserve_a = Decimal::from_f64(p.reserve_a).unwrap_or_default();
    let reserve_b = Decimal::from_f64(p.reserve_b).unwrap_or_default();

    let a = a_token_rate
        .checked_mul(reserve_a)
        .context(ArithmeticOverflowSnafu)?;

    let b = b_token_rate
        .checked_mul(reserve_b)
        .context(ArithmeticOverflowSnafu)?;

    let res = a.checked_add(b).context(ArithmeticOverflowSnafu)?;

    Ok(res)
}

pub async fn get_total_liquidity_usd(ctx: &Arc<AppContext>, p: &PoolPairInfo) -> Result<Decimal> {
    let (a, b) = parse_pool_pair_symbol(&p.symbol)?;

    let reserve_a = Decimal::from_f64(p.reserve_a).unwrap_or_default();
    let reserve_b = Decimal::from_f64(p.reserve_b).unwrap_or_default();

    if ["DUSD", "USDT", "USDC"].contains(&a.as_str()) {
        return reserve_a
            .checked_mul(dec!(2))
            .context(ArithmeticOverflowSnafu);
    };

    if ["DUSD", "USDT", "USDC"].contains(&b.as_str()) {
        return reserve_b
            .checked_mul(dec!(2))
            .context(ArithmeticOverflowSnafu);
    };

    let usdt_per_dfi = get_usd_per_dfi(ctx).await?;
    if usdt_per_dfi.is_zero() {
        return Ok(usdt_per_dfi);
    };

    if a == "DFI" {
        return reserve_a
            .checked_mul(dec!(2))
            .context(ArithmeticOverflowSnafu)?
            .checked_mul(usdt_per_dfi)
            .context(ArithmeticOverflowSnafu);
    };

    if b == "DFI" {
        return reserve_b
            .checked_mul(dec!(2))
            .context(ArithmeticOverflowSnafu)?
            .checked_mul(usdt_per_dfi)
            .context(ArithmeticOverflowSnafu);
    };

    let res = get_total_liquidity_usd_by_best_path(ctx, p).await?;
    Ok(res)
}

fn calculate_rewards(accounts: &[String], dfi_price_usdt: Decimal) -> Result<Decimal> {
    let rewards = accounts.iter().try_fold(dec!(0), |accumulate, account| {
        let (amount, token) = parse_amount(account)?;

        if token != "0" && token != "DFI" {
            return Ok(accumulate);
        }

        let yearly = Decimal::from_str(&amount)?
            .checked_mul(dec!(2880))
            .and_then(|v| v.checked_mul(dec!(365)))
            .and_then(|v| v.checked_mul(dfi_price_usdt))
            .context(ArithmeticOverflowSnafu)?;
        accumulate
            .checked_add(yearly)
            .context(ArithmeticOverflowSnafu)
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
        .and_then(serde_json::Value::as_f64) // eg: { "LP_DAILY_DFI_REWARD": 3664.80000000 }
        .unwrap_or_default();

    let daily_dfi_reward = Decimal::from_f64(reward).context(DecimalConversionSnafu)?;
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
        .context(ArithmeticOverflowSnafu)?
        .checked_mul(dec!(365))
        .context(ArithmeticOverflowSnafu)?
        .checked_mul(dfi_price_usd)
        .context(ArithmeticOverflowSnafu)
}

async fn get_block_subsidy(eunos_height: u32, height: u32) -> Result<Decimal> {
    let eunos_height = Decimal::from_u32(eunos_height).context(DecimalConversionSnafu)?;
    let height = Decimal::from_u32(height).context(DecimalConversionSnafu)?;
    let mut block_subsidy = dec!(405.04);

    if height >= eunos_height {
        let reduction_amount = dec!(0.01658); // 1.658%
        let mut reductions = height
            .checked_sub(eunos_height)
            .context(ArithmeticUnderflowSnafu)?
            .checked_div(dec!(32690))
            .context(ArithmeticUnderflowSnafu)?
            .floor();

        while reductions >= dec!(0) {
            let amount = reduction_amount
                .checked_mul(block_subsidy)
                .context(ArithmeticOverflowSnafu)?;
            if amount <= dec!(0.00001) {
                return Ok(dec!(0));
            };
            block_subsidy = block_subsidy
                .checked_sub(amount)
                .context(ArithmeticUnderflowSnafu)?;
            reductions = reductions
                .checked_sub(dec!(1))
                .context(ArithmeticUnderflowSnafu)?;
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
        .context(OtherSnafu {
            msg: "BlockchainInfo eunos height field is missing",
        })?;

    get_block_subsidy(eunos_height, info.blocks).await
}

async fn get_yearly_reward_loan_usd(ctx: &Arc<AppContext>, id: &String) -> Result<Decimal> {
    let splits = get_loan_token_splits(ctx).await?;
    let value = splits.unwrap_or_default();
    let split = value
        .as_object()
        .and_then(|obj| obj.get(id))
        .and_then(serde_json::Value::as_f64)
        .unwrap_or_default();
    let split = Decimal::from_f64(split).context(DecimalConversionSnafu)?;

    let dfi_price_usd = get_usd_per_dfi(ctx).await?;

    let loan_emission = get_loan_emission(ctx).await?;

    loan_emission
        .checked_mul(split) // 60 * 60 * 24 / 30, 30 seconds = 1 block
        .context(ArithmeticOverflowSnafu)?
        .checked_mul(dec!(2880)) // 60 * 60 * 24 / 30, 30 seconds = 1 block
        .context(ArithmeticOverflowSnafu)?
        .checked_mul(dec!(365)) // 60 * 60 * 24 / 30, 30 seconds = 1 block
        .context(ArithmeticOverflowSnafu)?
        .checked_mul(dfi_price_usd) // 60 * 60 * 24 / 30, 30 seconds = 1 block
        .context(ArithmeticOverflowSnafu)
}

async fn gather_amount(
    ctx: &Arc<AppContext>,
    pool_id: u32,
    interval: u32,
    count: usize,
) -> Result<Decimal> {
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

    let mut aggregated = HashMap::<u64, Decimal>::new();

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
                amount
                    .checked_add(from_amount)
                    .context(ArithmeticOverflowSnafu)?
            } else {
                from_amount
            };

            aggregated.insert(*token_id, amount);
        }
    }

    let mut volume = dec!(0);

    for token_id in aggregated.keys() {
        let token_price = get_token_usd_value(ctx, token_id).await?;
        let amount = aggregated.get(token_id).copied().unwrap_or(dec!(0));
        volume = volume
            .checked_add(
                token_price
                    .checked_mul(amount)
                    .context(ArithmeticOverflowSnafu)?,
            )
            .context(ArithmeticOverflowSnafu)?;
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
async fn get_yearly_commission_estimate(
    ctx: &Arc<AppContext>,
    id: &str,
    p: &PoolPairInfo,
) -> Result<Decimal> {
    let volume = get_usd_volume(ctx, id).await?;
    let commission = Decimal::from_f64(p.commission).unwrap_or_default();
    commission
        .checked_mul(volume.h24)
        .context(ArithmeticOverflowSnafu)?
        .checked_mul(dec!(365))
        .context(ArithmeticOverflowSnafu)
}

pub async fn get_apr(
    ctx: &Arc<AppContext>,
    id: &String,
    p: &PoolPairInfo,
) -> Result<PoolPairAprResponse> {
    let custom_usd = get_yearly_custom_reward_usd(ctx, p).await?;
    let pct_usd = get_yearly_reward_pct_usd(ctx, p).await?;
    let loan_usd = get_yearly_reward_loan_usd(ctx, id).await?;
    let total_liquidity_usd = get_total_liquidity_usd(ctx, p).await?;

    let yearly_usd = custom_usd
        .checked_add(pct_usd)
        .context(ArithmeticOverflowSnafu)?
        .checked_add(loan_usd)
        .context(ArithmeticOverflowSnafu)?;

    if yearly_usd.is_zero() {
        return Ok(PoolPairAprResponse::default());
    };

    // 1 == 100%, 0.1 = 10%
    let reward = yearly_usd
        .checked_div(total_liquidity_usd)
        .context(ArithmeticUnderflowSnafu)?;

    let yearly_commission = get_yearly_commission_estimate(ctx, id, p).await?;
    let commission = yearly_commission
        .checked_div(total_liquidity_usd)
        .context(ArithmeticUnderflowSnafu)?;

    let total = reward
        .checked_add(commission)
        .context(ArithmeticOverflowSnafu)?;

    Ok(PoolPairAprResponse {
        total,
        reward,
        commission,
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

async fn get_token_usd_value(ctx: &Arc<AppContext>, token_id: &u64) -> Result<Decimal> {
    let info = ain_cpp_imports::get_dst_token(token_id.to_string());
    if info.is_null() {
        return Err(Error::NotFound {
            kind: NotFoundKind::Token {
                id: token_id.to_string(),
            },
        });
    }

    if ["DUSD", "USDT", "USDC"].contains(&info.symbol.as_str()) {
        return Ok(dec!(1));
    };

    let dusd_pool = get_pool_pair(ctx, &info.symbol, "DUSD").await?;
    if let Some(p) = dusd_pool {
        let (a, _) = parse_pool_pair_symbol(&p.symbol)?;
        let reserve_a = Decimal::from_f64(p.reserve_a).context(DecimalConversionSnafu)?;
        let reserve_b = Decimal::from_f64(p.reserve_b).context(DecimalConversionSnafu)?;
        if a == "DUSD" {
            return reserve_a
                .checked_div(reserve_b)
                .context(ArithmeticUnderflowSnafu);
        };
        return reserve_b
            .checked_div(reserve_a)
            .context(ArithmeticUnderflowSnafu);
    }

    let dfi_pool = get_pool_pair(ctx, &info.symbol, "DFI").await?;
    if let Some(p) = dfi_pool {
        let usd_per_dfi = get_usd_per_dfi(ctx).await?;
        let reserve_a = Decimal::from_f64(p.reserve_a).context(DecimalConversionSnafu)?;
        let reserve_b = Decimal::from_f64(p.reserve_b).context(DecimalConversionSnafu)?;
        if p.id_token_a == *"0" {
            return reserve_a
                .checked_div(reserve_b)
                .context(ArithmeticUnderflowSnafu)?
                .checked_mul(usd_per_dfi)
                .context(ArithmeticOverflowSnafu);
        }
        return reserve_b
            .checked_div(reserve_a)
            .context(ArithmeticUnderflowSnafu)?
            .checked_mul(usd_per_dfi)
            .context(ArithmeticOverflowSnafu);
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
            .context(ArithmeticOverflowSnafu)?
            .checked_mul(amount)
            .context(ArithmeticOverflowSnafu)?;
    }

    Ok(value)
}

fn call_dftx(ctx: &Arc<AppContext>, txid: Txid) -> Result<Option<DfTx>> {
    let vout = ctx
        .services
        .transaction
        .vout_by_id
        .list(Some((txid, 0)), SortOrder::Ascending)?
        .take(1)
        .take_while(|item| match item {
            Ok((_, vout)) => vout.txid == txid,
            _ => true,
        })
        .map(|item| {
            let (_, v) = item?;
            Ok(v)
        })
        .collect::<Result<Vec<_>>>()?;

    if vout.is_empty() {
        return Ok(None);
    }

    let bytes = &vout[0].script.hex;
    if bytes.len() > 6 && bytes[0] == 0x6a && bytes[1] <= 0x4e {
        let offset = 1 + match bytes[1] {
            0x4c => 2,
            0x4d => 3,
            0x4e => 4,
            _ => 1,
        };

        let raw_tx = &bytes[offset..];
        let dftx = match deserialize::<Stack>(raw_tx) {
            Ok(stack) => stack.dftx,
            Err(e) => return Err(e.into()),
        };
        return Ok(Some(dftx));
    };

    Ok(None)
}

fn find_composite_swap_dftx(ctx: &Arc<AppContext>, txid: Txid) -> Result<Option<CompositeSwap>> {
    let Some(dftx) = call_dftx(ctx, txid)? else {
        return Ok(None);
    };

    let composite_swap_dftx = match dftx {
        DfTx::CompositeSwap(data) => Some(data),
        _ => None,
    };
    // let pool_swap_dftx = match dftx {
    //     DfTx::PoolSwap(data) => Some(data),
    //     DfTx::CompositeSwap(data) => Some(data.pool_swap),
    //     _ => None,
    // };;

    Ok(composite_swap_dftx)
}

pub async fn find_swap_from(
    ctx: &Arc<AppContext>,
    swap: &PoolSwap,
) -> Result<Option<PoolSwapFromToData>> {
    let PoolSwap {
        from,
        from_amount,
        from_token_id,
        ..
    } = swap;
    let from_address = from_script(from, ctx.network)?;

    let Some((_, from_token)) = get_token_cached(ctx, &from_token_id.to_string()).await? else {
        return Ok(None);
    };

    Ok(Some(PoolSwapFromToData {
        address: from_address,
        amount: Decimal::new(from_amount.to_owned(), 8).to_string(),
        display_symbol: parse_display_symbol(&from_token),
        symbol: from_token.symbol,
    }))
}

pub async fn find_swap_to(
    ctx: &Arc<AppContext>,
    swap: &PoolSwap,
) -> Result<Option<PoolSwapFromToData>> {
    let PoolSwap {
        to,
        to_token_id,
        to_amount,
        ..
    } = swap;
    let to_address = from_script(to, ctx.network)?;

    let Some((_, to_token)) = get_token_cached(ctx, &to_token_id.to_string()).await? else {
        return Ok(None);
    };

    let display_symbol = parse_display_symbol(&to_token);

    Ok(Some(PoolSwapFromToData {
        address: to_address,
        amount: Decimal::new(to_amount.to_owned(), 8).to_string(),
        symbol: to_token.symbol,
        display_symbol,
    }))
}

async fn get_pool_swap_type(ctx: &Arc<AppContext>, swap: &PoolSwap) -> Result<Option<SwapType>> {
    let Some((_, pool_pair_info)) = get_pool_pair_cached(ctx, swap.pool_id.to_string()).await?
    else {
        return Ok(None);
    };

    let id_token_a = pool_pair_info.id_token_a.parse::<u64>()?;
    let swap_type = if id_token_a == swap.from_token_id {
        SwapType::SELL
    } else {
        SwapType::BUY
    };
    Ok(Some(swap_type))
}

pub async fn check_swap_type(ctx: &Arc<AppContext>, swap: &PoolSwap) -> Result<Option<SwapType>> {
    let Some(dftx) = find_composite_swap_dftx(ctx, swap.txid)? else {
        return get_pool_swap_type(ctx, swap).await;
    };

    if dftx.pools.iter().count() <= 1 {
        return get_pool_swap_type(ctx, swap).await;
    }

    let mut prev = swap.from_token_id.to_string();
    for pool in dftx.pools.iter() {
        let pool_id = pool.id.0.to_string();
        let Some((_, pool_pair_info)) = get_pool_pair_cached(ctx, pool_id.clone()).await? else {
            break;
        };

        // if this is current pool pair, if previous token is primary token, indicator = sell
        if pool_id == swap.pool_id.to_string() {
            let swap_type = if pool_pair_info.id_token_a == prev {
                SwapType::SELL
            } else {
                SwapType::BUY
            };
            return Ok(Some(swap_type));
        }
        // set previous token as pair swapped out token
        prev = if prev == pool_pair_info.id_token_a {
            pool_pair_info.id_token_b
        } else {
            pool_pair_info.id_token_a
        }
    }

    Ok(None)
}
