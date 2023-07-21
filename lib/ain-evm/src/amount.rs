use primitive_types::U256;

pub struct WeiAmount(pub U256);

pub const WEI_TO_GWEI: U256 = U256([1_000_000_000, 0, 0, 0]);
pub const GWEI_TO_SATS: U256 = U256([10, 0, 0, 0]);

impl WeiAmount {
    pub fn to_gwei(&self) -> U256 {
        self.0 / WEI_TO_GWEI
    }

    pub fn to_satoshi(&self) -> U256 {
        self.0 / WEI_TO_GWEI / GWEI_TO_SATS
    }
}

pub fn from_gwei(gwei: U256) -> WeiAmount {
    WeiAmount(gwei * WEI_TO_GWEI)
}

pub fn from_satoshi(satoshi: U256) -> WeiAmount {
    WeiAmount(satoshi * GWEI_TO_SATS * WEI_TO_GWEI)
}
