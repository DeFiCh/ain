use std::env;
use std::path::PathBuf;

fn main() {
    let manifest_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let lib_path = &manifest_path
        .parent()
        .unwrap()
        .parent()
        .unwrap()
        .join("src");

    cxx_build::bridge("src/lib.rs")
        .include(lib_path)
        .flag_if_supported("-std=c++17")
        .flag_if_supported("-Wno-unused-parameter")
        .compile("ain-evm-cpp-ffi");

    println!(
        "cargo:rerun-if-changed={}/lib.rs",
        lib_path.to_str().unwrap()
    );
    println!(
        "cargo:rerun-if-changed={}/masternodes/ffi_exports.h",
        lib_path.to_str().unwrap()
    );
}
