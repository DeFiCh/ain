use std::sync::Arc;

use dftx_rs::oracles::*;

use super::Context;
use crate::{
    indexer::{Index, Result},
    Services,
};

impl Index for AppointOracle {
    fn index(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        todo!()
    }
}

impl Index for RemoveOracle {
    fn index(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        todo!()
    }
}

impl Index for UpdateOracle {
    fn index(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        todo!()
    }
}

impl Index for SetOracleData {
    fn index(&self, services: &Arc<Services>, ctx: &Context) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, services: &Arc<Services>, context: &Context) -> Result<()> {
        todo!()
    }
}
