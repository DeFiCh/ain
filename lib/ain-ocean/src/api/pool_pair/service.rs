use std::{str::FromStr, sync::Arc};
use anyhow::format_err;
use defichain_rpc::json::poolpair::PoolPairInfo;
use rust_decimal::{prelude::FromPrimitive, Decimal};
use rust_decimal_macros::dec;

use super::AppContext;

use crate::{
    api::cache::{get_pool_pair_cached, get_token_cached},
    api::pool_pair::path::{get_best_path, BestSwapPathResponse},
    Error, Result,
};

async fn get_usd_per_dfi(ctx: &Arc<AppContext>) -> Result<Decimal> {
  let usdt = get_pool_pair_cached(ctx, "USDT-DFI".to_string()).await?;

  let usdc = get_pool_pair_cached(ctx, "USDC-DFI".to_string()).await?;

  let mut total_usd = dec!(0);
  let mut total_dfi= dec!(0);

  fn add(p: PoolPairInfo, mut total_usd: Decimal, mut total_dfi: Decimal) -> Result<(Decimal, Decimal)> {
    let reserve_a = Decimal::from_f64(p.reserve_a).unwrap_or_default();
    let reserve_b = Decimal::from_f64(p.reserve_b).unwrap_or_default();
    if p.id_token_a == "0" {
        total_usd = total_usd.checked_add(reserve_b).ok_or_else(|| format_err!("total_usd overflow"))?;
        total_dfi = total_dfi.checked_add(reserve_a).ok_or_else(|| format_err!("total_dfi overflow"))?;
    } else if p.id_token_b == "0" {
        total_usd = total_usd.checked_add(reserve_a).ok_or_else(|| format_err!("total_usd overflow"))?;
        total_dfi = total_dfi.checked_add(reserve_b).ok_or_else(|| format_err!("total_dfi overflow"))?;
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
      return Ok(total_usd.checked_div(total_dfi).ok_or_else(|| format_err!("total_usd underflow"))?)
  };

  Ok(dec!(0))
}

async fn get_total_liquidity_usd_by_best_path(ctx: &Arc<AppContext>, p: &PoolPairInfo) -> Result<Decimal> {
  let (usdt_id, _) = get_token_cached(ctx, "USDT").await?.unwrap();

  let mut a_token_rate = dec!(1) ;
  let mut b_token_rate = dec!(1) ;

  if p.id_token_a != usdt_id {
    let BestSwapPathResponse {
      estimated_return,
      ..
    } = get_best_path(ctx, &p.id_token_a, &usdt_id).await?;
    a_token_rate = Decimal::from_str(estimated_return.as_str()).map_err(|e| format_err!(e))?;
  }

  if p.id_token_a != usdt_id {
    let BestSwapPathResponse {
      estimated_return,
      ..
    } = get_best_path(ctx, &p.id_token_b, &usdt_id).await?;
    b_token_rate = Decimal::from_str(estimated_return.as_str()).map_err(|e| format_err!(e))?;
  }

  let reserve_a = Decimal::from_f64(p.reserve_a).unwrap_or_default();
  let reserve_b = Decimal::from_f64(p.reserve_b).unwrap_or_default();

  let a = a_token_rate
    .checked_mul(reserve_a)
    .ok_or_else(|| format_err!("overflow"))?;

  let b = b_token_rate
    .checked_mul(reserve_b)
    .ok_or_else(|| format_err!("overflow"))?;

  let res = a.checked_add(b).ok_or_else(|| format_err!("overflow"))?;

  Ok(res)
}

pub async fn get_total_liquidity_usd(ctx: &Arc<AppContext>, p: &PoolPairInfo) -> Result<Decimal> {
  let parts = p.symbol.split('-').collect::<Vec<&str>>();
  let [a, b] = <[&str; 2]>::try_from(parts).ok().unwrap();

  let reserve_a = Decimal::from_f64(p.reserve_a).unwrap_or_default();
  let reserve_b = Decimal::from_f64(p.reserve_b).unwrap_or_default();

  if ["DUSD", "USDT", "USDC"].contains(&a) {
      return Ok(reserve_a.checked_mul(dec!(2)).ok_or_else(|| format_err!("total_liquidity_usd overflow"))?)
  };

  if ["DUSD", "USDT", "USDC"].contains(&b) {
      return Ok(reserve_b.checked_mul(dec!(2)).ok_or_else(|| format_err!("total_liquidity_usd overflow"))?)
  };

  let usdt_per_dfi = get_usd_per_dfi(ctx).await?;
  if usdt_per_dfi.is_zero() {
      return Ok(usdt_per_dfi)
  };

  if a == "DFI" {
      return Ok(reserve_a
          .checked_mul(dec!(2))
          .ok_or_else(|| format_err!("total_liquidity_usd overflow"))?
          .checked_mul(usdt_per_dfi)
          .ok_or_else(|| format_err!("total_liquidity_usd overflow"))?)
  };

  if b == "DFI" {
      return Ok(reserve_b
          .checked_mul(dec!(2))
          .ok_or_else(|| format_err!("total_liquidity_usd overflow"))?
          .checked_mul(usdt_per_dfi)
          .ok_or_else(|| format_err!("total_liquidity_usd overflow"))?)
  };

  let res = get_total_liquidity_usd_by_best_path(ctx, p).await?;
  Ok(res)
}