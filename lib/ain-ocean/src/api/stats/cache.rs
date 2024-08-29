use std::{collections::HashMap, str::FromStr, sync::Arc};

use cached::proc_macro::cached;
use defichain_rpc::{
    defichain_rpc_json::token::TokenPagination, json::account::AccountAmount, AccountRPC, Client,
    LoanRPC, TokenRPC,
};
use rust_decimal::{
    prelude::{FromPrimitive, Zero},
    Decimal,
};
use rust_decimal_macros::dec;
use serde::{Deserialize, Serialize};
use snafu::OptionExt;

use super::{subsidy::BLOCK_SUBSIDY, COIN};
use crate::{
    api::{
        cache::list_pool_pairs_cached,
        common::find_token_balance,
        pool_pair::service::{get_total_liquidity_usd, get_usd_per_dfi},
        stats::get_block_reward_distribution,
        AppContext,
    },
    error::{DecimalConversionSnafu, OtherSnafu},
    model::MasternodeStatsData,
    storage::{RepositoryOps, SortOrder},
    Result,
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
    time = 600,
    key = "String",
    convert = r#"{ format!("burned") }"#
)]
pub async fn get_burned(client: &Client) -> Result<Burned> {
    let burn_info = client.get_burn_info().await?;

    let utxo = Decimal::from_f64(burn_info.amount).context(DecimalConversionSnafu)?;
    let emission =
        Decimal::from_f64(burn_info.emissionburn).context(DecimalConversionSnafu)?;
    let fee = Decimal::from_f64(burn_info.feeburn).context(DecimalConversionSnafu)?;
    let auction = Decimal::from_f64(burn_info.auctionburn).context(DecimalConversionSnafu)?;

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
    pub prices: usize,
    pub masternodes: u32,
}

#[cached(
    result = true,
    time = 600,
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

    let prices = ctx
        .services
        .price_ticker
        .by_id
        .list(None, SortOrder::Descending)?
        .collect::<Vec<_>>();

    Ok(Count {
        blocks: 0,
        tokens: tokens.0.len(),
        masternodes,
        prices: prices.len(),
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
    time = 600,
    key = "String",
    convert = r#"{ format!("burned_total") }"#
)]
pub async fn get_burned_total(ctx: &AppContext) -> Result<Decimal> {
    let burn_address = BURN_ADDRESS
        .get(ctx.network.as_str())
        .context(OtherSnafu { msg: "Missing burn address" })?;
    let accounts = ctx
        .client
        .get_account(burn_address, None, Some(true))
        .await?;
    let burn_info = ctx.client.get_burn_info().await?;

    let utxo = Decimal::from_f64(burn_info.amount).context(DecimalConversionSnafu)?;
    let emission =
        Decimal::from_f64(burn_info.emissionburn).context(DecimalConversionSnafu)?;
    let fee = Decimal::from_f64(burn_info.feeburn).context(DecimalConversionSnafu)?;
    let account_balance = if let AccountAmount::List(accounts) = accounts {
        for account in accounts {
            let mut parts = account.split('@');

            let amount = parts.next().context(OtherSnafu { msg: "Missing amount" })?;
            let token_id = parts.next().context(OtherSnafu { msg: "Missing token_id" })?;

            if token_id == "DFI" {
                return Ok(Decimal::from_str(amount).unwrap_or_default());
            }
        }
        dec!(0)
    } else {
        dec!(0)
    };

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
    time = 600,
    key = "String",
    convert = r#"{ format!("emission") }"#
)]
pub fn get_emission(height: u32) -> Result<Emission> {
    let subsidy = Decimal::from_u64(BLOCK_SUBSIDY.get_block_subsidy(height))
        .context(DecimalConversionSnafu)?;
    let distribution = get_block_reward_distribution(subsidy);

    let masternode = distribution.masternode;
    let dex = distribution.liquidity;
    let community = distribution.community;
    let anchor = distribution.anchor;
    let total = subsidy / Decimal::from(COIN);
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
    time = 600,
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
    time = 600,
    key = "String",
    convert = r#"{ format!("masternodes") }"#
)]
pub async fn get_masternodes(ctx: &Arc<AppContext>) -> Result<Masternodes> {
    let stats = ctx
        .services
        .masternode
        .stats
        .get_latest()?
        .map_or(MasternodeStatsData::default(), |mn| mn.stats);

    let usd = get_usd_per_dfi(ctx).await?;

    Ok(Masternodes {
        locked: stats
            .locked
            .into_iter()
            .map(|(k, v)| Locked {
                weeks: k,
                tvl: v.tvl * usd,
                count: v.count,
            })
            .collect(),
    })
}

#[derive(Debug, Serialize, Deserialize, Default, Clone)]
pub struct Tvl {
    pub total: Decimal,
    pub dex: Decimal,
    pub loan: Decimal,
    pub masternodes: Decimal,
}

#[cached(
    result = true,
    time = 600,
    key = "String",
    convert = r#"{ format!("tvl") }"#
)]
pub async fn get_tvl(ctx: &Arc<AppContext>) -> Result<Tvl> {
    // dex
    let mut dex = dec!(0);
    let pools = list_pool_pairs_cached(ctx, None, None).await?.0;
    for (_, info) in pools {
        let total_liquidity_usd = get_total_liquidity_usd(ctx, &info).await?;
        dex += total_liquidity_usd;
    }

    // masternodes
    let usd = get_usd_per_dfi(ctx).await?;
    let mut masternodes = ctx
        .services
        .masternode
        .stats
        .get_latest()?
        .map_or(Decimal::zero(), |mn| mn.stats.tvl);
    masternodes *= usd;

    // loan
    let loan = get_loan(&ctx.client).await?;
    let loan = Decimal::from_f64(loan.value.collateral).unwrap_or_default();

    Ok(Tvl {
        loan,
        masternodes,
        dex,
        total: dex + masternodes + loan,
    })
}

#[derive(Debug, Serialize, Deserialize, Default, Clone)]
pub struct Price {
    pub usd: Decimal,
    #[deprecated(note = "use USD instead of aggregation over multiple pairs")]
    pub usdt: Decimal,
}

#[cached(
    result = true,
    time = 600,
    key = "String",
    convert = r#"{ format!("price") }"#
)]
pub async fn get_price(ctx: &Arc<AppContext>) -> Result<Price> {
    let usd = get_usd_per_dfi(ctx).await?;
    #[allow(deprecated)]
    Ok(Price { usd, usdt: usd })
}
