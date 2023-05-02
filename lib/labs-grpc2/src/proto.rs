pub mod eth {
    include!(concat!(env!("OUT_DIR"), "/eth.rs"));
    // include!(concat!(env!("CARGO_MANIFEST_DIR"), "/gen", "/eth.rs"));
}

// macro_rules! blackhole { ($tt:x) => () }
// blackhole!(1e0);
