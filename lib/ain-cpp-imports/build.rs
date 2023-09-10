use std::{env, path::PathBuf};

use anyhow::{format_err, Result};

fn main() -> Result<()> {
    let pkg_name = env::var("CARGO_PKG_NAME")?;
    let manifest_dir_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR")?);

    let cpp_src_path = &manifest_dir_path
        .parent()
        .and_then(std::path::Path::parent)
        .map(|x| x.join("src"))
        .ok_or(format_err!("path err"))?;

    let ffi_rs_src_path = &manifest_dir_path.join("src/bridge.rs");
    let ffi_exports_h_path = &cpp_src_path.join("ffi/ffiexports.h");

    let mut cxx = cxx_build::bridge(ffi_rs_src_path);
    cxx.include(cpp_src_path)
        .flag("-std=c++17")
        .flag("-Wno-unused-parameter")
        .cpp_link_stdlib(if cfg!(target_os = "macos") {
            "c++"
        } else {
            "stdc++"
        });

    // Note: For windows, we set the defines to the correct headers are used.
    // The cfg! targets can't be used, since the detection heuristic for cc-rs
    // is slightly different. But we can infer it from TARGET
    let target = env::var("TARGET")?;
    if target.contains("windows") {
        cxx.define("_MT", None)
            .define("WIN32", None)
            .define("_WIN32", None)
            .define("__GLIBCXX__", None)
            // .define("WIN32_LEAN_AND_MEAN", None)
            .define("_WINDOWS", None)
            .define("_WIN32_WINNT", "0x0601")
            .static_flag(true);
    }

    cxx.compile(pkg_name.as_str());

    let path_utf8_err = || format_err!("path utf8 err");

    println!(
        "cargo:rerun-if-changed={}",
        ffi_rs_src_path.to_str().ok_or_else(path_utf8_err)?
    );
    println!(
        "cargo:rerun-if-changed={}",
        &ffi_exports_h_path.to_str().ok_or_else(path_utf8_err)?
    );

    Ok(())
}
