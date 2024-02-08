use serde::{Deserialize, Serialize};

lazy_static::lazy_static! {
    // TODO handle networks
    // Global service caching all block subsidy reductions
    pub static ref BLOCK_SUBSIDY: BlockSubsidy = BlockSubsidy::new(TEST_NET_COINBASE_SUBSIDY_OPTIONS);
}

#[derive(Serialize, Deserialize, Debug, Clone, Copy)]
pub struct CoinbaseSubsidyOptions {
    eunos_height: u64,
    genesis_block_subsidy: u64,
    pre_eunos_block_subsidy: u64,
    eunos_base_block_subsidy: u64,
    eunos_foundation_burn: u64,
    emission_reduction: u64,
    emission_reduction_interval: u64,
}

pub static MAIN_NET_COINBASE_SUBSIDY_OPTIONS: CoinbaseSubsidyOptions = CoinbaseSubsidyOptions {
    eunos_height: 894000,
    genesis_block_subsidy: 59100003000000000,
    pre_eunos_block_subsidy: 20000000000,
    eunos_base_block_subsidy: 40504000000,
    eunos_foundation_burn: 26859289307829046,
    emission_reduction: 1658,
    emission_reduction_interval: 32690,
};

pub static TEST_NET_COINBASE_SUBSIDY_OPTIONS: CoinbaseSubsidyOptions = CoinbaseSubsidyOptions {
    eunos_height: 354950,
    genesis_block_subsidy: 30400004000000000,
    pre_eunos_block_subsidy: 20000000000,
    eunos_base_block_subsidy: 40504000000,
    eunos_foundation_burn: 0,
    emission_reduction: 1658,
    emission_reduction_interval: 32690,
};

pub struct BlockSubsidy {
    reduction_block_subsidies: Vec<u64>,
    reduction_supply_milestones: Vec<u64>,
    options: CoinbaseSubsidyOptions,
}

impl BlockSubsidy {
    pub fn new(options: CoinbaseSubsidyOptions) -> Self {
        let reduction_block_subsidies = BlockSubsidy::compute_block_reduction_subsidies(&options);
        let reduction_supply_milestones =
            BlockSubsidy::compute_reduction_supply_milestones(&reduction_block_subsidies, &options);
        BlockSubsidy {
            reduction_block_subsidies,
            reduction_supply_milestones,
            options,
        }
    }

    pub fn get_supply(&self, height: u32) -> u64 {
        let height = u64::from(height);
        if height < self.options.eunos_height {
            self.get_pre_eunos_supply(height)
        } else {
            self.get_post_eunos_supply(height)
        }
    }

    pub fn get_block_subsidy(&self, height: u32) -> u64 {
        let height = u64::from(height);
        if height == 0 {
            return self.options.genesis_block_subsidy;
        }

        if height < self.options.eunos_height {
            return self.options.pre_eunos_block_subsidy;
        }

        let reduction_count =
            (height - self.options.eunos_height) / self.options.emission_reduction_interval;
        if reduction_count < self.reduction_block_subsidies.len() as u64 {
            return self.reduction_block_subsidies[reduction_count as usize];
        }

        0
    }

    fn get_pre_eunos_supply(&self, height: u64) -> u64 {
        self.options.genesis_block_subsidy + self.options.pre_eunos_block_subsidy * height
    }

    fn get_post_eunos_supply(&self, height: u64) -> u64 {
        let post_eunos_diff = height - (self.options.eunos_height - 1);
        let reduction_count = post_eunos_diff / self.options.emission_reduction_interval;
        let reduction_remainder = post_eunos_diff % self.options.emission_reduction_interval;

        if reduction_count >= self.reduction_supply_milestones.len() as u64 {
            *self.reduction_supply_milestones.last().unwrap()
        } else {
            self.reduction_supply_milestones[reduction_count as usize]
                + self.reduction_block_subsidies[reduction_count as usize] * reduction_remainder
        }
    }

    fn compute_reduction_supply_milestones(
        reduction_block_subsidies: &[u64],
        options: &CoinbaseSubsidyOptions,
    ) -> Vec<u64> {
        let mut supply_milestones = vec![
            options.genesis_block_subsidy
                + options.pre_eunos_block_subsidy * (options.eunos_height - 1)
                - options.eunos_foundation_burn,
        ];
        for i in 1..reduction_block_subsidies.len() {
            let previous_milestone = supply_milestones[i - 1];
            supply_milestones.push(
                previous_milestone
                    + reduction_block_subsidies[i - 1] * options.emission_reduction_interval,
            );
        }
        supply_milestones
    }

    fn compute_block_reduction_subsidies(options: &CoinbaseSubsidyOptions) -> Vec<u64> {
        let mut subsidy_reductions: Vec<u64> = vec![options.eunos_base_block_subsidy];
        while let Some(&last_subsidy) = subsidy_reductions.last() {
            let amount = last_subsidy * options.emission_reduction / 100000;
            if amount == 0 {
                break;
            }
            subsidy_reductions.push(last_subsidy - amount);
        }
        subsidy_reductions
    }
}
