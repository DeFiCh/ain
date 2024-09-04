use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Network {
    Mainnet,
    Mocknet,
    Testnet,
    Regtest,
    Devnet,
    Changi,
}

impl Network {
    #[must_use]
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Mainnet => "mainnet",
            Self::Mocknet => "mocknet",
            Self::Testnet => "testnet",
            Self::Regtest => "regtest",
            Self::Devnet => "devnet",
            Self::Changi => "changi",
        }
    }
}

#[allow(clippy::from_over_into)]
impl Into<bitcoin::Network> for Network {
    fn into(self) -> bitcoin::Network {
        match self {
            Self::Mainnet => bitcoin::Network::Mainnet,
            Self::Testnet => bitcoin::Network::Testnet,
            Self::Devnet => bitcoin::Network::Devnet,
            Self::Regtest => bitcoin::Network::Regtest,
            _ => bitcoin::Network::Regtest,
        }
    }
}

impl std::str::FromStr for Network {
    type Err = &'static str;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "mainnet" | "main" => Ok(Self::Mainnet),
            "mocknet" => Ok(Self::Mocknet),
            "testnet" => Ok(Self::Testnet),
            "regtest" => Ok(Self::Regtest),
            "devnet" => Ok(Self::Devnet),
            "changi" => Ok(Self::Changi),
            _ => Err("invalid network"),
        }
    }
}

impl fmt::Display for Network {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Mainnet => write!(f, "mainnet"),
            Self::Mocknet => write!(f, "mocknet"),
            Self::Testnet => write!(f, "testnet"),
            Self::Regtest => write!(f, "regtest"),
            Self::Devnet => write!(f, "devnet"),
            Self::Changi => write!(f, "changi"),
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
    #[must_use]
    pub fn params(&self) -> NetworkParams {
        match self {
            Self::Mainnet | Self::Testnet | Self::Devnet | Self::Changi | Self::Mocknet => {
                NetworkParams {
                    activation_delay: 10,
                    new_activation_delay: 1008,
                    resign_delay: 60,
                    new_resign_delay: 2016,
                }
            }
            Self::Regtest => NetworkParams {
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
    #[must_use]
    pub fn fork_heights(&self) -> ForkHeight {
        match self {
            Self::Mainnet | Self::Mocknet => ForkHeight {
                df1_amk_height: 356_500,                         // Oct 12th, 2020.,
                df2_bayfront_height: 405_000,                    // Nov 2nd, 2020.,
                df3_bayfront_marina_height: 465_150,             // Nov 28th, 2020.,
                df4_bayfront_gardens_height: 488_300,            // Dec 8th, 2020.,
                df5_clarke_quay_height: 595_738,                 // Jan 24th, 2021.,
                df6_dakota_height: 678_000,                      // Mar 1st, 2021.,
                df7_dakota_crescent_height: 733_000,             // Mar 25th, 2021.,
                df8_eunos_height: 894_000,                       // Jun 3rd, 2021.,
                df9_eunos_kampung_height: 895_743,               // Jun 4th, 2021.,
                df10_eunos_paya_height: 1_072_000,               // Aug 5th, 2021.,
                df11_fort_canning_height: 1_367_000,             // Nov 15th, 2021.,
                df12_fort_canning_museum_height: 1_430_640,      // Dec 7th, 2021.,
                df13_fort_canning_park_height: 1_503_143,        // Jan 2nd, 2022.,
                df14_fort_canning_hill_height: 1_604_999,        // Feb 7th, 2022.,
                df15_fort_canning_road_height: 1_786_000,        // April 11th, 2022.,
                df16_fort_canning_crunch_height: 1_936_000,      // June 2nd, 2022.,
                df17_fort_canning_spring_height: 2_033_000,      // July 6th, 2022.,
                df18_fort_canning_great_world_height: 2_212_000, // Sep 7th, 2022.,
                df19_fort_canning_epilogue_height: 2_257_500,    // Sep 22nd, 2022.,
                df20_grand_central_height: 2_479_000,            // Dec 8th, 2022.,
                df21_grand_central_epilogue_height: 2_574_000,   // Jan 10th, 2023.,
                df22_metachain_height: 3_462_000,                // Nov 15th, 2023.,
                df23_height: u32::MAX,
                df24_height: u32::MAX,
            },
            Self::Testnet => ForkHeight {
                df1_amk_height: 150,
                df2_bayfront_height: 3000,
                df3_bayfront_marina_height: 90470,
                df4_bayfront_gardens_height: 101_342,
                df5_clarke_quay_height: 155_000,
                df6_dakota_height: 220_680,
                df7_dakota_crescent_height: 287_700,
                df8_eunos_height: 354_950,
                df9_eunos_kampung_height: 354_950,
                df10_eunos_paya_height: 463_300,
                df11_fort_canning_height: 686_200,
                df12_fort_canning_museum_height: 724_000,
                df13_fort_canning_park_height: 828_800,
                df14_fort_canning_hill_height: 828_900,
                df15_fort_canning_road_height: 893_700,
                df16_fort_canning_crunch_height: 1_011_600,
                df17_fort_canning_spring_height: 1_086_000,
                df18_fort_canning_great_world_height: 1_150_000,
                df19_fort_canning_epilogue_height: 1_150_010,
                df20_grand_central_height: 1_150_020,
                df21_grand_central_epilogue_height: 1_150_030,
                df22_metachain_height: 1_150_040,
                df23_height: 1_507_200,
                df24_height: u32::MAX,
            },
            Self::Regtest => ForkHeight {
                df1_amk_height: 10_000_000,
                df2_bayfront_height: 10_000_000,
                df3_bayfront_marina_height: 10_000_000,
                df4_bayfront_gardens_height: 10_000_000,
                df5_clarke_quay_height: 10_000_000,
                df6_dakota_height: 10_000_000,
                df7_dakota_crescent_height: 10_000_000,
                df8_eunos_height: 10_000_000,
                df9_eunos_kampung_height: 10_000_000,
                df10_eunos_paya_height: 10_000_000,
                df11_fort_canning_height: 10_000_000,
                df12_fort_canning_museum_height: 10_000_000,
                df13_fort_canning_park_height: 10_000_000,
                df14_fort_canning_hill_height: 10_000_000,
                df15_fort_canning_road_height: 10_000_000,
                df16_fort_canning_crunch_height: 10_000_000,
                df17_fort_canning_spring_height: 10_000_000,
                df18_fort_canning_great_world_height: 10_000_000,
                df19_fort_canning_epilogue_height: 10_000_000,
                df20_grand_central_height: 10_000_000,
                df21_grand_central_epilogue_height: 10_000_000,
                df22_metachain_height: 10_000_000,
                df23_height: 10_000_000,
                df24_height: 10_000_000,
            },
            Self::Devnet | Self::Changi => ForkHeight {
                df1_amk_height: 150,
                df2_bayfront_height: 3000,
                df3_bayfront_marina_height: 90470,
                df4_bayfront_gardens_height: 101_342,
                df5_clarke_quay_height: 155_000,
                df6_dakota_height: 220_680,
                df7_dakota_crescent_height: 287_700,
                df8_eunos_height: 354_950,
                df9_eunos_kampung_height: 354_950,
                df10_eunos_paya_height: 463_300,
                df11_fort_canning_height: 686_200,
                df12_fort_canning_museum_height: 724_000,
                df13_fort_canning_park_height: 828_800,
                df14_fort_canning_hill_height: 828_900,
                df15_fort_canning_road_height: 893_700,
                df16_fort_canning_crunch_height: 1_011_600,
                df17_fort_canning_spring_height: 1_086_000,
                df18_fort_canning_great_world_height: 1_223_000,
                df19_fort_canning_epilogue_height: 1_244_000,
                df20_grand_central_height: 1_366_000,
                df21_grand_central_epilogue_height: 1_438_200,
                df22_metachain_height: 1_586_750,
                df23_height: 1_985_600,
                df24_height: u32::MAX,
            },
        }
    }
}
