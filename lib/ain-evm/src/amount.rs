use primitive_types::U256;

pub struct Amount(pub U256);
pub type Wei = Amount;
pub type GWei = Amount;
pub type Satoshi = Amount;

pub const WEI_TO_GWEI: U256 = U256([1_000_000_000, 0, 0, 0]);
pub const GWEI_TO_SATS: U256 = U256([10, 0, 0, 0]);

impl Amount {
    pub fn wei_to_gwei(&self) -> GWei {
        Amount(self.0 / WEI_TO_GWEI)
    }

    pub fn wei_to_satoshi(&self) -> Satoshi {
        Amount(self.0 / WEI_TO_GWEI / GWEI_TO_SATS)
    }

    pub fn gwei_to_wei(&self) -> Wei {
        Amount(self.0 * WEI_TO_GWEI)
    }

    pub fn gwei_to_satoshi(&self) -> Satoshi {
        Amount(self.0 / GWEI_TO_SATS)
    }

    pub fn satoshi_to_gwei(&self) -> Wei {
        Amount(self.0 * GWEI_TO_SATS)
    }

    pub fn satoshi_to_wei(&self) -> Wei {
        Amount(self.0 * GWEI_TO_SATS * WEI_TO_GWEI)
    }
}
