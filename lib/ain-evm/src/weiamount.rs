use anyhow::format_err;
use ethereum_types::U256;

use crate::Result;

pub struct WeiAmount(pub U256);

pub const WEI_TO_GWEI: U256 = U256([1_000_000_000, 0, 0, 0]);
pub const WEI_TO_SATS: U256 = U256([10_000_000_000, 0, 0, 0]);
pub const WEI_TO_SATS_DOUBLE: f64 = 10_000_000_000.0;
pub const GWEI_TO_SATS: U256 = U256([10, 0, 0, 0]);
pub const MAX_MONEY_SATS: U256 = U256([120_000_000_000_000_000, 0, 0, 0]);
pub const MAX_MONEY_SATS_DOUBLE: f64 = 120_000_000_000_000_000.0;

impl WeiAmount {
    pub fn to_gwei(&self) -> U256 {
        self.0 / WEI_TO_GWEI
    }

    pub fn to_satoshi(&self) -> U256 {
        self.0 / WEI_TO_SATS
    }

    pub fn to_satoshi_double(&self) -> Result<f64> {
        if !self.wei_range() {
            return Err(format_err!("value more than money range").into());
        }

        let sats_q = u64::try_from(self.to_satoshi())? as f64;
        let wei_r = u64::try_from(self.0.checked_rem(WEI_TO_SATS).unwrap_or(U256::zero()))? as f64;
        let sats_r = wei_r / WEI_TO_SATS_DOUBLE;
        Ok(sats_q + sats_r)
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

#[cfg(test)]
mod tests {
    use crate::weiamount::{try_from_satoshi, WeiAmount, MAX_MONEY_SATS, MAX_MONEY_SATS_DOUBLE};
    use ethereum_types::U256;
    use std::error::Error;

    #[test]
    fn test_satoshi_double_conversion() -> Result<(), Box<dyn Error>> {
        // 5.45 sats
        let sats = WeiAmount(U256::from(54_500_000_000u64));
        let sats_double = 5.45f64;
        let sats_dbl_res = sats.to_satoshi_double()?;
        assert_eq!(sats_double, sats_dbl_res);
        Ok(())
    }

    #[test]
    fn test_max_satoshi_conversion() -> Result<(), Box<dyn Error>> {
        let max_sats = try_from_satoshi(MAX_MONEY_SATS)?;
        let max_sats_res = max_sats.to_satoshi();
        let max_sats_dbl_res = max_sats.to_satoshi_double()?;
        assert_eq!(MAX_MONEY_SATS, max_sats_res);
        assert_eq!(MAX_MONEY_SATS_DOUBLE, max_sats_dbl_res);
        Ok(())
    }
}
