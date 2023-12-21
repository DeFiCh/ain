mod db;
mod ocean_store;

use std::path::PathBuf;

use ocean_store::OceanStore;

lazy_static::lazy_static! {
    // Global services exposed by the library
    pub static ref OCEAN_STORE: OceanStore = {
        let datadir = ain_cpp_imports::get_datadir();
        OceanStore::new(&PathBuf::from(datadir)).expect("Error initialization Ocean storage")
    };
}
