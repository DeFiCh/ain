use std::env;
use std::path::PathBuf;

fn main() {
    let pkg_name = env::var("CARGO_PKG_NAME").unwrap();
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
        .compile(pkg_name.as_str());

    println!(
        "cargo:rerun-if-changed={}/lib.rs",
        lib_path.to_str().unwrap()
    );
    println!(
        "cargo:rerun-if-changed={}/ffi/ffiexports.h",
        lib_path.to_str().unwrap()
    );
}
