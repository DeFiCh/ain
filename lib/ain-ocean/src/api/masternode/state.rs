use serde::{Deserialize, Serialize};

use crate::{model::Masternode, network::Network};

#[derive(Serialize, Deserialize, Debug, Default, Clone)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum MasternodeState {
    PreEnabled,
    Enabled,
    PreResigned,
    Resigned,
    PreBanned,
    Banned,
    #[default]
    Unknown,
}

pub struct MasternodeService {
    network: Network,
}

impl MasternodeService {
    pub fn new(network: Network) -> Self {
        MasternodeService { network }
    }

    fn get_mn_activation_delay(&self, height: u32) -> u32 {
        let eunos_height = self.network.fork_heights().df8_eunos_height;
        if height < eunos_height {
            self.network.params().activation_delay
        } else {
            self.network.params().new_activation_delay
        }
    }

    fn get_mn_resign_delay(&self, height: u32) -> u32 {
        let eunos_height = self.network.fork_heights().df8_eunos_height;
        if height < eunos_height {
            self.network.params().resign_delay
        } else {
            self.network.params().new_resign_delay
        }
    }

    pub fn get_masternode_state(&self, masternode: &Masternode, height: u32) -> MasternodeState {
        if let Some(resign_height) = masternode.resign_height {
            let resign_delay = self.get_mn_resign_delay(resign_height);
            if height < resign_height + resign_delay {
                MasternodeState::PreResigned
            } else {
                MasternodeState::Resigned
            }
        } else {
            let activation_delay = self.get_mn_activation_delay(masternode.creation_height);
            if masternode.creation_height == 0
                || height >= masternode.creation_height + activation_delay
            {
                MasternodeState::Enabled
            } else {
                MasternodeState::PreEnabled
            }
        }
    }
}
