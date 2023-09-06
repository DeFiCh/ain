use anyhow::format_err;
use ethereum_types::U256;

use crate::Result;

pub struct WeiAmount(pub U256);

pub const WEI_TO_GWEI: U256 = U256([1_000_000_000, 0, 0, 0]);
pub const WEI_TO_SATS: U256 = U256([10_000_000_000, 0, 0, 0]);
pub const GWEI_TO_SATS: U256 = U256([10, 0, 0, 0]);
pub const MAX_MONEY_SATS: U256 = U256([120_000_000_000_000_000, 0, 0, 0]);

impl WeiAmount {
    pub fn to_gwei(&self) -> U256 {
        self.0 / WEI_TO_GWEI
    }

    pub fn to_satoshi(&self) -> U256 {
        self.0 / WEI_TO_SATS
    }

    pub fn wei_range(&self) -> bool {
        let max_money_sats = MAX_MONEY_SATS;
        let max_money_wei = max_money_sats.saturating_mul(WEI_TO_SATS);
        self.0 <= max_money_wei
    }
}

pub fn try_from_gwei(gwei: U256) -> Result<WeiAmount> {
    let wei = gwei.checked_mul(WEI_TO_GWEI);
    match wei {
        Some(wei) => Ok(WeiAmount(wei)),
        None => Err(format_err!("convert gwei to wei failed from overflow").into()),
    }
}

pub fn try_from_satoshi(satoshi: U256) -> Result<WeiAmount> {
    let wei = satoshi.checked_mul(WEI_TO_SATS);
    match wei {
        Some(wei) => Ok(WeiAmount(wei)),
        None => Err(format_err!("convert gwei to wei failed from overflow").into()),
    }
}
