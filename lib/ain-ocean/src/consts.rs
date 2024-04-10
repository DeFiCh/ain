use std::fmt;
#[derive(Debug, Clone)]
pub enum Network {
    Mainnet,
    Testnet,
    Regtest,
    Devnet,
    Changi,
}

impl std::str::FromStr for Network {
    type Err = &'static str;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "mainnet" => Ok(Network::Mainnet),
            "testnet" => Ok(Network::Testnet),
            "regtest" => Ok(Network::Regtest),
            "devnet" => Ok(Network::Devnet),
            "changi" => Ok(Network::Changi),
            _ => Err("invalid network"),
        }
    }
}

impl fmt::Display for Network {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Network::Mainnet => write!(f, "mainnet"),
            Network::Testnet => write!(f, "testnet"),
            Network::Regtest => write!(f, "regtest"),
            Network::Devnet => write!(f, "devnet"),
            Network::Changi => write!(f, "changi"),
        }
    }
}
