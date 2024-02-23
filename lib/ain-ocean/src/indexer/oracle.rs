use std::sync::Arc;

use ain_dftx::oracles::*;

use super::Context;
use crate::{
    indexer::{Index, Result},
    Services,
};

impl Index for AppointOracle {
    fn index(&self, _services: &Arc<Services>, _ctx: &Context) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, _services: &Arc<Services>, _context: &Context) -> Result<()> {
        todo!()
    }
}

impl Index for RemoveOracle {
    fn index(&self, _services: &Arc<Services>, _ctx: &Context) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, _services: &Arc<Services>, _context: &Context) -> Result<()> {
        todo!()
    }
}

impl Index for UpdateOracle {
    fn index(&self, _services: &Arc<Services>, _ctx: &Context) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, _services: &Arc<Services>, _context: &Context) -> Result<()> {
        todo!()
    }
}

impl Index for SetOracleData {
    fn index(&self, _services: &Arc<Services>, _ctx: &Context) -> Result<()> {
        todo!()
    }

    fn invalidate(&self, _services: &Arc<Services>, _context: &Context) -> Result<()> {
        todo!()
    }
}
