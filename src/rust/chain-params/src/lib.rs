use std::str::FromStr;

#[derive(Debug)]
pub struct ChainParams {
    pub rpc_port: u16,
    pub base_dir: &'static str,
    pub network: &'static str,
}

#[derive(Debug)]
pub enum Chain {
    Main,
    Testnet,
    Devnet,
    Regtest,
}

impl FromStr for Chain {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "regtest" => Ok(Self::Regtest),
            "testnet" => Ok(Self::Testnet),
            "devnet" => Ok(Self::Devnet),
            "mainnet" => Ok(Self::Main),
            chain => Err(format!("Unsupported chain {}", chain)),
        }
    }
}

impl Chain {
    pub fn get_params(&self) -> ChainParams {
        match self {
            Self::Main => ChainParams {
                rpc_port: 8554,
                base_dir: "",
                network: "main",
            },
            Self::Testnet => ChainParams {
                rpc_port: 18554,
                base_dir: "testnet3",
                network: "testnet",
            },
            Self::Devnet => ChainParams {
                rpc_port: 20554,
                base_dir: "devnet",
                network: "devnet",
            },
            Self::Regtest => ChainParams {
                rpc_port: 19554,
                base_dir: "regtest",
                network: "regtest",
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_chain() {
        let main_chain = Chain::from_str("mainnet").unwrap();
        let main_params = main_chain.get_params();
        assert_eq!(main_params.rpc_port, 8554);
        assert_eq!(main_params.base_dir, "");
        assert_eq!(main_params.network, "main");
    }
}
