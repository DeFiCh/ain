use primitive_types::U256;

pub struct Wei(pub U256);
pub struct GWei(pub U256);
pub struct Satoshi(pub U256);

pub const WEI_TO_GWEI: U256 = U256([1_000_000_000, 0, 0, 0]);
pub const GWEI_TO_SATS: U256 = U256([10, 0, 0, 0]);

impl Wei {
    pub fn to_gwei(&self) -> GWei {
        GWei(self.0 / WEI_TO_GWEI)
    }

    pub fn to_satoshi(&self) -> Satoshi {
        Satoshi(self.0 / WEI_TO_GWEI / GWEI_TO_SATS)
    }
}

impl GWei {
    pub fn to_wei(&self) -> Wei {
        Wei(self.0 * WEI_TO_GWEI)
    }

    pub fn to_satoshi(&self) -> Satoshi {
        Satoshi(self.0 / GWEI_TO_SATS)
    }
}

impl Satoshi {
    pub fn to_wei(&self) -> Wei {
        Wei(self.0 * GWEI_TO_SATS * WEI_TO_GWEI)
    }

    pub fn to_gwei(&self) -> GWei {
        GWei(self.0 * GWEI_TO_SATS)
    }
}
