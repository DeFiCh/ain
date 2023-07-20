use keccak_hash::H256;

struct Wei(U256);
struct GWei(U256);
struct Satoshi(U256);

pub const WEI_TO_GWEI: Wei = 1_000_000_000;
pub const GWEI_TO_SATS: GWei = 10;


impl Wei {
    pub fn to_gwei(&self) -> GWei {
        Gwei(self.0 / WEI_TO_GWEI)
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

    pub fn to_gwei(&self) -> Gwei {
        Gwei(self.0 * GWEI_TO_SATS)
    }
}
