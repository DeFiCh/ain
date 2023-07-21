use proc_macro2::TokenStream;

use anyhow::anyhow;
use ethers_solc::{Project, ProjectPathsConfig};
use std::fs::File;
use std::io::{Read, Write};
use std::path::PathBuf;
use std::{env, fs};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let pkg_name = env::var("CARGO_PKG_NAME")?;
    let manifest_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR")?);

    // If TARGET_DIR is set, which we do from Makefile, uses that instead of OUT_DIR.
    // Otherwise, use the path for OUT_DIR that cargo sets, as usual.
    // Reason: Currently setting --out-dir is nightly only, so there's no way to get OUT_DIR
    // out of cargo reliably for pointing the C++ link targets to this determinisitcally.
    let target_dir: PathBuf = PathBuf::from(env::var("CARGO_TARGET_DIR").or(env::var("OUT_DIR"))?);
    let res = ["src", "include", "lib"].map(|x| target_dir.join(x));
    for x in &res {
        std::fs::create_dir_all(x)?;
    }
    let [out_src_dir, out_include_dir, _out_lib_dir] = res;

    let lib_path = &manifest_path.join("src").join("lib.rs");

    let mut content = String::new();
    File::open(lib_path)?.read_to_string(&mut content)?;

    let pkg_name_underscored = pkg_name.replace('-', "_");
    let header_file_path = pkg_name_underscored.clone() + ".h";
    let source_file_path = pkg_name_underscored + ".cpp";

    let tt: TokenStream = content.parse()?;
    let mut opt = cxx_gen::Opt::default();
    opt.include.push(cxx_gen::Include {
        path: header_file_path.clone(),
        kind: cxx_gen::IncludeKind::Bracketed,
    });

    let codegen = cxx_gen::generate_header_and_cc(tt, &opt)?;
    let cpp_stuff = String::from_utf8(codegen.implementation)?;

    File::create(out_include_dir.join(header_file_path))?.write_all(&codegen.header)?;
    File::create(out_src_dir.join(source_file_path))?.write_all(cpp_stuff.as_bytes())?;

    println!(
        "cargo:rerun-if-changed={}",
        lib_path.as_path().to_str().ok_or("lib path err")?
    );
    // Using a direct path for now
    let git_head_path = manifest_path.join("../../.git/HEAD");
    if git_head_path.exists() {
        println!(
            "cargo:rerun-if-changed={}",
            git_head_path.to_str().ok_or("git head path err")?
        );
    }

    // compile solidity project
    // configure `root` as our project root
    let root = PathBuf::from("counter_contract");
    if !root.exists() {
        return Err("Project root {root:?} does not exists!".into());
    }

    let paths = ProjectPathsConfig::builder()
        .root(&root)
        .sources(&root)
        .build()?;

    let project = Project::builder()
        .paths(paths)
        .set_auto_detect(true)
        .no_artifacts()
        .build()?;
    let output = project.compile().unwrap();
    let artifacts = output.into_artifacts();

    for (id, artifact) in artifacts {
        if id.name == "Counter" {
            let abi = artifact.abi.ok_or_else(|| anyhow!("ABI not found"))?;
            let bytecode = artifact.deployed_bytecode.expect("No bytecode found");

            fs::create_dir_all("counter_contract/output/")?;
            fs::write(
                PathBuf::from("counter_contract/output/bytecode.json"),
                serde_json::to_string(&bytecode).unwrap().as_bytes(),
            )?;
            fs::write(
                PathBuf::from("counter_contract/output/abi.json"),
                serde_json::to_string(&abi).unwrap().as_bytes(),
            )?;
        }
    }

    project.rerun_if_sources_changed();

    Ok(())
}
