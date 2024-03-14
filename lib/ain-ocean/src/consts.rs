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

impl Network {
    pub fn as_str(&self) -> &'static str {
        match self {
            Network::Mainnet => "mainnet",
            Network::Testnet => "testnet",
            Network::Regtest => "regtest",
            Network::Devnet => "devnet",
            Network::Changi => "changi",
        }
    }
}
