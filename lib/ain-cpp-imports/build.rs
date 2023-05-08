use anyhow::{format_err, Result};
use std::env;
use std::path::PathBuf;

fn main() -> Result<()> {
    let pkg_name = env::var("CARGO_PKG_NAME")?;
    let manifest_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR")?);

    let cpp_src_path = &manifest_path
        .parent()
        .and_then(std::path::Path::parent)
        .map(|x| x.join("src"))
        .ok_or(format_err!("path err"))?;

    let ffi_rs_src_path = &manifest_path.join("src/bridge.rs");
    let ffi_exports_h_path = &cpp_src_path.join("ffi/ffiexports.h");

    cxx_build::bridge(ffi_rs_src_path)
        .include(cpp_src_path)
        .cpp_link_stdlib(if cfg!(target_os = "macos") {
            "c++"
        } else {
            "stdc++"
        })
        .flag("-std=c++17")
        .flag("-Wno-unused-parameter")
        .static_flag(true)
        .compile(pkg_name.as_str());

    let path_utf8_err = || format_err!("path utf8 err");

    println!(
        "cargo:rerun-if-changed={}",
        ffi_rs_src_path.to_str().ok_or_else(path_utf8_err)?
    );
    println!(
        "cargo:rerun-if-changed={}",
        &ffi_exports_h_path.to_str().ok_or_else(path_utf8_err)?
    );
    // Using a direct path for now
    let git_head_path = manifest_path.join("../../.git/HEAD");
    if git_head_path.exists() {
        println!(
            "cargo:rerun-if-changed={}",
            git_head_path.to_str().ok_or_else(path_utf8_err)?
        );
    }

    Ok(())
}
