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

impl ToString for Network {
    fn to_string(&self) -> String {
        match self {
            Network::Mainnet => String::from("mainnet"),
            Network::Testnet => String::from("testnet"),
            Network::Regtest => String::from("regtest"),
            Network::Devnet => String::from("devnet"),
            Network::Changi => String::from("changi"),
        }
    }
}
