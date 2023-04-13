use proc_macro2::TokenStream;

use std::env;
use std::fs::File;
use std::io::{Read, Write};
use std::path::PathBuf;

fn main() {
    let _pkg_name = env::var("CARGO_PKG_NAME").unwrap();
    let manifest_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());

    // TODO: Use root path to force re-run during development
    if std::path::Path::new(".git/HEAD").exists() {
        println!("cargo:rerun-if-changed=.git/HEAD");
    }

    let target_dir = if let Ok(v) = env::var("BUILD_DIR") {
        PathBuf::from(v)
    } else {
        manifest_path.clone()
    };
    println!("BUILD_DIR: {:?}", target_dir);
    std::fs::create_dir_all(&target_dir).unwrap();

    let parent = manifest_path.clone();

    let lib_path = &parent.join("src").join("lib.rs");

    let mut content = String::new();
    File::open(lib_path)
        .unwrap()
        .read_to_string(&mut content)
        .unwrap();

    let tt: TokenStream = content.parse().unwrap();
    let mut opt = cxx_gen::Opt::default();
    opt.include.push(cxx_gen::Include {
        path: "libain_evm.h".to_string(),
        kind: cxx_gen::IncludeKind::Bracketed,
    });

    let codegen = cxx_gen::generate_header_and_cc(tt, &opt).unwrap();
    let cpp_stuff = String::from_utf8(codegen.implementation).unwrap();

    File::create(target_dir.join("libain_evm.h"))
        .unwrap()
        .write_all(&codegen.header)
        .unwrap();
    File::create(target_dir.join("libain_evm.cpp"))
        .unwrap()
        .write_all(cpp_stuff.as_bytes())
        .unwrap();
}
