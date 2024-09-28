mod cache;
mod distribution;
mod subsidy;

use std::sync::Arc;

use ain_dftx::COIN;
use ain_macros::ocean_endpoint;
use axum::{routing::get, Extension, Router};
use defichain_rpc::{
    defichain_rpc_json::{account::BurnInfo, GetNetworkInfoResult},
    AccountRPC,
};
use rust_decimal::{prelude::FromPrimitive, Decimal};
use serde::{Deserialize, Serialize};
use snafu::OptionExt;

use self::{
    cache::{
        get_burned, get_burned_total, get_count, get_emission, get_loan, get_masternodes,
        get_price, get_tvl, Burned, Count, Emission, Loan, Masternodes, Price, Tvl,
    },
    distribution::get_block_reward_distribution,
    subsidy::BLOCK_SUBSIDY,
};
use super::{cache::get_network_info_cached, response::Response, AppContext};
use crate::{
    error::{ApiError, DecimalConversionSnafu},
    Result,
};

#[derive(Debug, Serialize, Deserialize, Default)]
pub struct StatsData {
    pub count: Count,
    pub tvl: Tvl,
    pub burned: Burned,
    pub price: Price,
    pub masternodes: Masternodes,
    pub emission: Emission,
    pub loan: Loan,
    pub blockchain: Blockchain,
    pub net: Net,
}

#[derive(Debug, Serialize, Deserialize, Default)]
pub struct Blockchain {
    pub difficulty: f64,
}

#[derive(Debug, Serialize, Deserialize, Default)]
pub struct Net {
    pub version: u64,
    pub subversion: String,
    pub protocolversion: u64,
}

#[ocean_endpoint]
async fn get_stats(Extension(ctx): Extension<Arc<AppContext>>) -> Result<Response<StatsData>> {
    let (height, difficulty) = ctx
        .services
        .block
        .by_height
        .get_highest()?
        .map(|b| (b.height, b.difficulty))
        .unwrap_or_default(); // Default to genesis block

    let GetNetworkInfoResult {
        version,
        subversion,
        protocol_version,
        ..
    } = get_network_info_cached(&ctx).await?;

    let stats = StatsData {
        burned: get_burned(&ctx.client).await?,
        net: Net {
            version: version as u64,
            protocolversion: protocol_version as u64,
            subversion,
        },
        count: Count {
            blocks: height,
            ..get_count(&ctx).await?
        },
        emission: get_emission(height)?,
        blockchain: Blockchain { difficulty },
        loan: get_loan(&ctx.client).await?,
        price: get_price(&ctx).await?,
        masternodes: get_masternodes(&ctx).await?,
        tvl: get_tvl(&ctx).await?,
    };
    Ok(Response::new(stats))
}

#[derive(Debug, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct RewardDistributionData {
    anchor: Decimal,
    community: Decimal,
    liquidity: Decimal,
    loan: Decimal,
    masternode: Decimal,
    options: Decimal,
    unallocated: Decimal,
}

#[ocean_endpoint]
async fn get_reward_distribution(
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<RewardDistributionData>> {
    let height = ctx
        .services
        .block
        .by_height
        .get_highest()?
        .map(|b| b.height)
        .unwrap_or_default(); // Default to genesis block

    let subsidy = Decimal::from_u64(BLOCK_SUBSIDY.get_block_subsidy(height))
        .context(DecimalConversionSnafu)?;
    let distribution = get_block_reward_distribution(subsidy);

    let distribution = RewardDistributionData {
        masternode: distribution.masternode.trunc_with_scale(8),
        anchor: distribution.anchor.trunc_with_scale(8),
        community: distribution.community.trunc_with_scale(8),
        liquidity: distribution.liquidity.trunc_with_scale(8),
        loan: distribution.loan.trunc_with_scale(8),
        options: distribution.options.trunc_with_scale(8),
        unallocated: distribution.unallocated.trunc_with_scale(8),
    };
    Ok(Response::new(distribution))
}

#[derive(Debug, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct SupplyData {
    max: u32,
    total: Decimal,
    burned: Decimal,
    circulating: Decimal,
}

#[ocean_endpoint]
async fn get_supply(Extension(ctx): Extension<Arc<AppContext>>) -> Result<Response<SupplyData>> {
    static MAX: u32 = 1_200_000_000;
    let height = ctx
        .services
        .block
        .by_height
        .get_highest()?
        .map(|b| b.height)
        .unwrap_or_default(); // Default to genesis block

    let total = Decimal::from_u64(BLOCK_SUBSIDY.get_supply(height))
        .context(DecimalConversionSnafu)?
        / Decimal::from(COIN);

    let burned = get_burned_total(&ctx).await?;
    let circulating = total - burned;

    let supply = SupplyData {
        max: MAX,
        total,
        burned,
        circulating,
    };
    Ok(Response::new(supply))
}

#[ocean_endpoint]
async fn get_burn(Extension(ctx): Extension<Arc<AppContext>>) -> Result<Response<BurnInfo>> {
    let burn_info = ctx.client.get_burn_info().await?;
    Ok(Response::new(burn_info))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/", get(get_stats))
        .route("/reward/distribution", get(get_reward_distribution))
        .route("/supply", get(get_supply))
        .route("/burn", get(get_burn))
        .layer(Extension(ctx))
}
