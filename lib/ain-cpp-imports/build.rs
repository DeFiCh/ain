use std::env;
use std::path::PathBuf;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let pkg_name = env::var("CARGO_PKG_NAME")?;
    let manifest_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR")?);

    let cpp_src_path = &manifest_path.parent()
        .and_then(|x| x.parent())
        .and_then(|x| Some(x.join("src")));
    
    let cpp_src_path = match cpp_src_path.as_ref() {
        Some(r) => Ok(r),
        None => Err("path err")
    }?;

    let ffi_rs_src_path = &manifest_path.join("src/bridge.rs");
    let ffi_exports_h_path = &cpp_src_path.join("ffi/ffiexports.h");

    cxx_build::bridge(ffi_rs_src_path)
        .include(&cpp_src_path)
        .cpp_link_stdlib("stdc++")
        .flag_if_supported("-std=c++17")
        .flag_if_supported("-Wno-unused-parameter")
        .compile(pkg_name.as_str());

    println!("cargo:rerun-if-changed={}", ffi_rs_src_path.to_string_lossy());
    println!("cargo:rerun-if-changed={}", &ffi_exports_h_path.to_string_lossy());

    Ok(())
}
