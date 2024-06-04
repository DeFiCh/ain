use std::fmt;
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Network {
    Mainnet,
    Mocknet,
    Testnet,
    Regtest,
    Devnet,
    Changi,
}

impl Network {
    pub fn as_str(&self) -> &'static str {
        match self {
            Network::Mainnet => "mainnet",
            Network::Mocknet => "mocknet",
            Network::Testnet => "testnet",
            Network::Regtest => "regtest",
            Network::Devnet => "devnet",
            Network::Changi => "changi",
        }
    }
}

// impl From<bitcoin::Network> for Network {
//     fn from(network: bitcoin::Network) -> Self {
//         match network {
//             bitcoin::Network::Mainnet => Network::Mainnet,
//             bitcoin::Network::Testnet => Network::Testnet,
//             bitcoin::Network::Devnet => Network::Devnet,
//             _ => Network::Regtest,
//         }
//     }
// }

impl Into<bitcoin::Network> for Network {
    fn into(self) -> bitcoin::Network {
        match self {
            Network::Mainnet => bitcoin::Network::Mainnet,
            Network::Testnet => bitcoin::Network::Testnet,
            Network::Devnet => bitcoin::Network::Devnet,
            Network::Regtest => bitcoin::Network::Regtest,
            _ => bitcoin::Network::Regtest,
        }

    }
}

impl std::str::FromStr for Network {
    type Err = &'static str;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "mainnet" | "main" => Ok(Network::Mainnet),
            "mocknet" => Ok(Network::Mocknet),
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
            Network::Mocknet => write!(f, "mocknet"),
            Network::Testnet => write!(f, "testnet"),
            Network::Regtest => write!(f, "regtest"),
            Network::Devnet => write!(f, "devnet"),
            Network::Changi => write!(f, "changi"),
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub struct NetworkParams {
    pub activation_delay: u32,
    pub new_activation_delay: u32,
    pub resign_delay: u32,
    pub new_resign_delay: u32,
}

impl Network {
    pub fn params(&self) -> NetworkParams {
        match self {
            Network::Mainnet
            | Network::Testnet
            | Network::Devnet
            | Network::Changi
            | Network::Mocknet => NetworkParams {
                activation_delay: 10,
                new_activation_delay: 1008,
                resign_delay: 60,
                new_resign_delay: 2016,
            },
            Network::Regtest => NetworkParams {
                activation_delay: 10,
                new_activation_delay: 20,
                resign_delay: 10,
                new_resign_delay: 40,
            },
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub struct ForkHeight {
    pub df1_amk_height: u32,
    pub df2_bayfront_height: u32,
    pub df3_bayfront_marina_height: u32,
    pub df4_bayfront_gardens_height: u32,
    pub df5_clarke_quay_height: u32,
    pub df6_dakota_height: u32,
    pub df7_dakota_crescent_height: u32,
    pub df8_eunos_height: u32,
    pub df9_eunos_kampung_height: u32,
    pub df10_eunos_paya_height: u32,
    pub df11_fort_canning_height: u32,
    pub df12_fort_canning_museum_height: u32,
    pub df13_fort_canning_park_height: u32,
    pub df14_fort_canning_hill_height: u32,
    pub df15_fort_canning_road_height: u32,
    pub df16_fort_canning_crunch_height: u32,
    pub df17_fort_canning_spring_height: u32,
    pub df18_fort_canning_great_world_height: u32,
    pub df19_fort_canning_epilogue_height: u32,
    pub df20_grand_central_height: u32,
    pub df21_grand_central_epilogue_height: u32,
    pub df22_metachain_height: u32,
    pub df23_height: u32,
    pub df24_height: u32,
}

impl Network {
    pub fn fork_heights(&self) -> ForkHeight {
        match self {
            Network::Mainnet | Network::Mocknet => ForkHeight {
                df1_amk_height: 356500,                        // Oct 12th, 2020.,
                df2_bayfront_height: 405000,                   // Nov 2nd, 2020.,
                df3_bayfront_marina_height: 465150,            // Nov 28th, 2020.,
                df4_bayfront_gardens_height: 488300,           // Dec 8th, 2020.,
                df5_clarke_quay_height: 595738,                // Jan 24th, 2021.,
                df6_dakota_height: 678000,                     // Mar 1st, 2021.,
                df7_dakota_crescent_height: 733000,            // Mar 25th, 2021.,
                df8_eunos_height: 894000,                      // Jun 3rd, 2021.,
                df9_eunos_kampung_height: 895743,              // Jun 4th, 2021.,
                df10_eunos_paya_height: 1072000,               // Aug 5th, 2021.,
                df11_fort_canning_height: 1367000,             // Nov 15th, 2021.,
                df12_fort_canning_museum_height: 1430640,      // Dec 7th, 2021.,
                df13_fort_canning_park_height: 1503143,        // Jan 2nd, 2022.,
                df14_fort_canning_hill_height: 1604999,        // Feb 7th, 2022.,
                df15_fort_canning_road_height: 1786000,        // April 11th, 2022.,
                df16_fort_canning_crunch_height: 1936000,      // June 2nd, 2022.,
                df17_fort_canning_spring_height: 2033000,      // July 6th, 2022.,
                df18_fort_canning_great_world_height: 2212000, // Sep 7th, 2022.,
                df19_fort_canning_epilogue_height: 2257500,    // Sep 22nd, 2022.,
                df20_grand_central_height: 2479000,            // Dec 8th, 2022.,
                df21_grand_central_epilogue_height: 2574000,   // Jan 10th, 2023.,
                df22_metachain_height: 3462000,                // Nov 15th, 2023.,
                df23_height: u32::MAX,
                df24_height: u32::MAX,
            },
            Network::Testnet => ForkHeight {
                df1_amk_height: 150,
                df2_bayfront_height: 3000,
                df3_bayfront_marina_height: 90470,
                df4_bayfront_gardens_height: 101342,
                df5_clarke_quay_height: 155000,
                df6_dakota_height: 220680,
                df7_dakota_crescent_height: 287700,
                df8_eunos_height: 354950,
                df9_eunos_kampung_height: 354950,
                df10_eunos_paya_height: 463300,
                df11_fort_canning_height: 686200,
                df12_fort_canning_museum_height: 724000,
                df13_fort_canning_park_height: 828800,
                df14_fort_canning_hill_height: 828900,
                df15_fort_canning_road_height: 893700,
                df16_fort_canning_crunch_height: 1011600,
                df17_fort_canning_spring_height: 1086000,
                df18_fort_canning_great_world_height: 1150000,
                df19_fort_canning_epilogue_height: 1150010,
                df20_grand_central_height: 1150020,
                df21_grand_central_epilogue_height: 1150030,
                df22_metachain_height: 1150040,
                df23_height: 1507200,
                df24_height: u32::MAX,
            },
            Network::Regtest => ForkHeight {
                df1_amk_height: 10000000,
                df2_bayfront_height: 10000000,
                df3_bayfront_marina_height: 10000000,
                df4_bayfront_gardens_height: 10000000,
                df5_clarke_quay_height: 10000000,
                df6_dakota_height: 10000000,
                df7_dakota_crescent_height: 10000000,
                df8_eunos_height: 10000000,
                df9_eunos_kampung_height: 10000000,
                df10_eunos_paya_height: 10000000,
                df11_fort_canning_height: 10000000,
                df12_fort_canning_museum_height: 10000000,
                df13_fort_canning_park_height: 10000000,
                df14_fort_canning_hill_height: 10000000,
                df15_fort_canning_road_height: 10000000,
                df16_fort_canning_crunch_height: 10000000,
                df17_fort_canning_spring_height: 10000000,
                df18_fort_canning_great_world_height: 10000000,
                df19_fort_canning_epilogue_height: 10000000,
                df20_grand_central_height: 10000000,
                df21_grand_central_epilogue_height: 10000000,
                df22_metachain_height: 10000000,
                df23_height: 10000000,
                df24_height: 10000000,
            },
            Network::Devnet | Network::Changi => ForkHeight {
                df1_amk_height: 150,
                df2_bayfront_height: 3000,
                df3_bayfront_marina_height: 90470,
                df4_bayfront_gardens_height: 101342,
                df5_clarke_quay_height: 155000,
                df6_dakota_height: 220680,
                df7_dakota_crescent_height: 287700,
                df8_eunos_height: 354950,
                df9_eunos_kampung_height: 354950,
                df10_eunos_paya_height: 463300,
                df11_fort_canning_height: 686200,
                df12_fort_canning_museum_height: 724000,
                df13_fort_canning_park_height: 828800,
                df14_fort_canning_hill_height: 828900,
                df15_fort_canning_road_height: 893700,
                df16_fort_canning_crunch_height: 1011600,
                df17_fort_canning_spring_height: 1086000,
                df18_fort_canning_great_world_height: 1223000,
                df19_fort_canning_epilogue_height: 1244000,
                df20_grand_central_height: 1366000,
                df21_grand_central_epilogue_height: 1438200,
                df22_metachain_height: 1586750,
                df23_height: 1985600,
                df24_height: u32::MAX,
            },
        }
    }
}
