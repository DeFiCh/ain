use std::{collections::HashMap, sync::Arc};

use cached::proc_macro::cached;
use defichain_rpc::{
    defichain_rpc_json::token::TokenPagination, AccountRPC, Client, LoanRPC, TokenRPC,
};
use rust_decimal::{
    prelude::{FromPrimitive, Zero},
    Decimal,
};
use rust_decimal_macros::dec;
use serde::{Deserialize, Serialize};

use super::{subsidy::BLOCK_SUBSIDY, COIN};
use crate::{
    api::{common::find_token_balance, stats::get_block_reward_distribution, AppContext},
    model::MasternodeStatsData,
    Error, Result, Services,
};

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct Burned {
    pub address: Decimal,
    pub fee: Decimal,
    pub auction: Decimal,
    pub payback: Decimal,
    pub emission: Decimal,
    pub total: Decimal,
}

#[cached(
    result = true,
    time = 1800,
    key = "String",
    convert = r#"{ format!("burned") }"#
)]
pub async fn get_burned(client: &Client) -> Result<Burned> {
    let burn_info = client.get_burn_info().await?;

    let utxo = Decimal::from_f64(burn_info.amount).ok_or(Error::DecimalError)?;
    let emission = Decimal::from_f64(burn_info.emissionburn).ok_or(Error::DecimalError)?;
    let fee = Decimal::from_f64(burn_info.feeburn).ok_or(Error::DecimalError)?;
    let auction = Decimal::from_f64(burn_info.auctionburn).ok_or(Error::DecimalError)?;

    let account = find_token_balance(burn_info.tokens, "DFI");
    let address = utxo + account;
    let payback = find_token_balance(burn_info.paybackburn, "DFI");

    let burned = Burned {
        address,
        emission,
        fee,
        payback,
        auction,
        total: address + fee + auction + payback + emission,
    };
    Ok(burned)
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct Count {
    pub blocks: u32,
    pub tokens: usize,
    pub prices: u64,
    pub masternodes: u32,
}

#[cached(
    result = true,
    time = 1800,
    key = "String",
    convert = r#"{ format!("count") }"#
)]
pub async fn get_count(ctx: &Arc<AppContext>) -> Result<Count> {
    let tokens = ctx
        .client
        .list_tokens(
            Some(TokenPagination {
                limit: 1000,
                ..Default::default()
            }),
            Some(true),
        )
        .await?;

    let masternodes = ctx
        .services
        .masternode
        .stats
        .get_latest()?
        .map_or(0, |mn| mn.stats.count);

    Ok(Count {
        tokens: tokens.0.len(),
        masternodes,
        // TODO handle prices
        // prices: <prices>
        ..Default::default()
    })
}

// TODO Shove it into network struct when available
lazy_static::lazy_static! {
    pub static ref  BURN_ADDRESS: HashMap<&'static str, &'static str> = HashMap::from([
        ("mainnet", "8defichainBurnAddressXXXXXXXdRQkSm"),
        ("testnet", "7DefichainBurnAddressXXXXXXXdMUE5n"),
        ("devnet", "7DefichainBurnAddressXXXXXXXdMUE5n"),
        ("changi", "7DefichainBurnAddressXXXXXXXdMUE5n"),
        ("regtest", "mfburnZSAM7Gs1hpDeNaMotJXSGA7edosG"),
    ]);
}

#[cached(
    result = true,
    time = 1800,
    key = "String",
    convert = r#"{ format!("burned_total") }"#
)]
pub async fn get_burned_total(ctx: &AppContext) -> Result<Decimal> {
    let burn_address = BURN_ADDRESS.get(ctx.network.as_str()).unwrap();
    let mut tokens = ctx
        .client
        .get_account(&burn_address, None, Some(true))
        .await?;
    let burn_info = ctx.client.get_burn_info().await?;

    let utxo = Decimal::from_f64(burn_info.amount).ok_or(Error::DecimalError)?;
    let emission = Decimal::from_f64(burn_info.emissionburn).ok_or(Error::DecimalError)?;
    let fee = Decimal::from_f64(burn_info.feeburn).ok_or(Error::DecimalError)?;
    let account_balance = tokens
        .0
        .remove("0")
        .map_or(dec!(0), |v| Decimal::from_f64(v).unwrap_or_default());

    Ok(utxo + account_balance + emission + fee)
}

#[derive(Debug, Serialize, Deserialize, Default, Clone)]
pub struct Emission {
    pub masternode: Decimal,
    pub dex: Decimal,
    pub community: Decimal,
    pub anchor: Decimal,
    pub burned: Decimal,
    pub total: Decimal,
}

#[cached(
    result = true,
    time = 1800,
    key = "String",
    convert = r#"{ format!("emission") }"#
)]
pub fn get_emission(height: u32) -> Result<Emission> {
    let subsidy =
        Decimal::from_u64(BLOCK_SUBSIDY.get_block_subsidy(height)).ok_or(Error::DecimalError)?;
    let distribution = get_block_reward_distribution(subsidy);

    let masternode = distribution.masternode;
    let dex = distribution.liquidity;
    let community = distribution.community;
    let anchor = distribution.anchor;
    let total = subsidy / COIN;
    let burned = total - (masternode + dex + community + anchor);

    Ok(Emission {
        masternode: masternode.trunc_with_scale(8),
        dex: dex.trunc_with_scale(8),
        community: community.trunc_with_scale(8),
        anchor: anchor.trunc_with_scale(8),
        burned,
        total,
    })
}

#[derive(Debug, Serialize, Deserialize, Default, Clone)]
pub struct LoanCount {
    pub schemes: u64,
    pub loan_tokens: u64,
    pub collateral_tokens: u64,
    pub open_vaults: u64,
    pub open_auctions: u64,
}

#[derive(Debug, Serialize, Deserialize, Default, Clone)]
pub struct LoanValue {
    pub collateral: f64,
    pub loan: f64,
}

#[derive(Debug, Serialize, Deserialize, Default, Clone)]
pub struct Loan {
    pub count: LoanCount,
    pub value: LoanValue,
}

#[cached(
    result = true,
    time = 1800,
    key = "String",
    convert = r#"{ format!("loan") }"#
)]
pub async fn get_loan(client: &Client) -> Result<Loan> {
    let info = client.get_loan_info().await?;

    Ok(Loan {
        count: LoanCount {
            collateral_tokens: info.totals.collateral_tokens,
            loan_tokens: info.totals.loan_tokens,
            open_auctions: info.totals.open_auctions,
            open_vaults: info.totals.open_vaults,
            schemes: info.totals.schemes,
        },
        value: LoanValue {
            collateral: info.totals.collateral_value,
            loan: info.totals.loan_value,
        },
    })
}

#[derive(Debug, Serialize, Deserialize, Default, Clone)]
pub struct Locked {
    pub weeks: u16,
    pub tvl: Decimal,
    pub count: u32,
}

#[derive(Debug, Serialize, Deserialize, Default, Clone)]
pub struct Masternodes {
    pub locked: Vec<Locked>,
}

#[cached(
    result = true,
    time = 1800,
    key = "String",
    convert = r#"{ format!("masternodes") }"#
)]
pub fn get_masternodes(services: &Services) -> Result<Masternodes> {
    let stats = services
        .masternode
        .stats
        .get_latest()?
        .map_or(MasternodeStatsData::default(), |mn| mn.stats);

    // TODO Tvl * DUSD value
    Ok(Masternodes {
        locked: stats
            .locked
            .into_iter()
            .map(|(k, v)| Locked {
                weeks: k,
                tvl: v.tvl,
                count: v.count,
            })
            .collect(),
    })
}

#[derive(Debug, Serialize, Deserialize, Default, Clone)]
pub struct Tvl {
    pub total: Decimal,
    pub dex: Decimal,
    pub loan: f64,
    pub masternodes: Decimal,
}

#[cached(
    result = true,
    time = 300,
    key = "String",
    convert = r#"{ format!("tvl") }"#
)]
pub async fn get_tvl(ctx: &Arc<AppContext>) -> Result<Tvl> {
    // let mut dex = 0f64;
    // let pairs = ctx
    //     .client
    //     .list_pool_pairs(

    //             including_start: true,
    //             start: 0,
    //             limit: 1000,
    //         }),
    //         Some(true),
    //     )
    //     .await;

    let loan = get_loan(&ctx.client).await?;

    // TODO value in DUSD
    let masternodes = ctx
        .services
        .masternode
        .stats
        .get_latest()?
        .map_or(Decimal::zero(), |mn| mn.stats.tvl);

    // return {
    //   dex: dex.toNumber(),
    //   masternodes: masternodeTvlUSD,
    //   loan: loan.value.collateral,
    //   total: dex.toNumber() + masternodeTvlUSD + loan.value.collateral
    // }
    Ok(Tvl {
        loan: loan.value.collateral,
        masternodes,
        // TODO dex
        // TODO total
        ..Default::default()
    })
}
