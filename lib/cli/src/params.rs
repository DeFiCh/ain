use std::str::FromStr;

#[derive(Debug, Default)]
pub enum Chain {
    #[default]
    Main,
    Testnet,
    Changi,
    Devnet,
    Regtest,
}

impl FromStr for Chain {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "main" => Ok(Chain::Main),
            "testnet" => Ok(Chain::Testnet),
            "changi" => Ok(Chain::Changi),
            "devnet" => Ok(Chain::Devnet),
            "regtest" => Ok(Chain::Regtest),
            _ => Err(format!("Unknown chain: {s}")),
        }
    }
}

#[derive(Debug)]
pub struct BaseChainParams {
    pub data_dir: String,
    pub rpc_port: u16,
    pub grpc_port: u16,
    pub eth_rpc_port: u16,
}

impl BaseChainParams {
    pub fn create(chain: &Chain) -> Self {
        match chain {
            Chain::Main => Self {
                data_dir: String::new(),
                rpc_port: 8554,
                grpc_port: 8550,
                eth_rpc_port: 8551,
            },
            Chain::Testnet => Self {
                data_dir: "testnet3".to_string(),
                rpc_port: 18554,
                grpc_port: 18550,
                eth_rpc_port: 18551,
            },
            Chain::Changi => {
                if std::env::var("CHANGI_BOOTSTRAP").is_ok() {
                    Self {
                        data_dir: "changi".to_string(),
                        rpc_port: 18554,
                        grpc_port: 18550,
                        eth_rpc_port: 18551,
                    }
                } else {
                    Self {
                        data_dir: "changi".to_string(),
                        rpc_port: 20554,
                        grpc_port: 20550,
                        eth_rpc_port: 20551,
                    }
                }
            }
            Chain::Devnet => {
                if std::env::var("DEVNET_BOOTSTRAP").is_ok() {
                    Self {
                        data_dir: "devnet".to_string(),
                        rpc_port: 18554,
                        grpc_port: 18550,
                        eth_rpc_port: 18551,
                    }
                } else {
                    Self {
                        data_dir: "devnet".to_string(),
                        rpc_port: 21554,
                        grpc_port: 21550,
                        eth_rpc_port: 21551,
                    }
                }
            }
            Chain::Regtest => Self {
                data_dir: "regtest".to_string(),
                rpc_port: 19554,
                grpc_port: 19550,
                eth_rpc_port: 19551,
            },
        }
    }
}
