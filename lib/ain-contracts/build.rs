use anyhow::anyhow;
use ethers_solc::{Project, ProjectPathsConfig};
use std::fs;
use std::path::PathBuf;

fn main() -> Result<(), Box<dyn std::error::Error>> {
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
