pub fn add(left: usize, right: usize) -> usize {
    left + right
}

pub mod proto {
    // This is the target to be generated.
    // include!(concat!(env!("OUT_DIR"), "/eth.rs"));
    include!(concat!(env!("CARGO_MANIFEST_DIR"), "/gen", "/eth.rs"));
}

#[cfg(test)]
mod tests {

    use std::{
        any::{Any, TypeId},
        fmt::Debug,
        marker::PhantomData,
    };

    use serde::Serialize;

    use super::*;
    use crate::proto::*;

    #[test]
    fn it_works() {
        let result = add(2, 2);
        assert_eq!(result, 4);

        fn print_debug<T: Debug + Serialize>(val: T) {
            println!("{:?}\n{}", val, serde_json::to_string_pretty(&val).unwrap());
        }

        let mut x = proto::EthCallResult::default();
        x.data = "hello".to_string();
        print_debug(x);

        let mut x = proto::EthSyncingResult::default();
        x.status_or_info = Some(eth_syncing_result::StatusOrInfo::Status(true));
        print_debug(x);

        let mut x = proto::EthSyncingResult::default();
        x.status_or_info = Some(eth_syncing_result::StatusOrInfo::SyncInfo(
            EthSyncingInfo::default(),
        ));
        print_debug(x);
    }
}
