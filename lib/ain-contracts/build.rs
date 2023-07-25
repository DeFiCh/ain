use anyhow::anyhow;
use ethers_solc::{Project, ProjectPathsConfig};
use std::fs;
use std::path::PathBuf;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // compile solidity project
    // configure `root` as our project root
    let contracts = vec![("counter_contract", "Counter"), ("dst20", "DST20")];

    for (file_path, contract_name) in contracts {
        let root = PathBuf::from(file_path);
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
            if id.name == contract_name {
                let abi = artifact.abi.ok_or_else(|| anyhow!("ABI not found"))?;
                let bytecode = artifact.deployed_bytecode.expect("No bytecode found");

                fs::create_dir_all(format!("{file_path}/output/"))?;
                fs::write(
                    PathBuf::from(format!("{file_path}/output/bytecode.json")),
                    serde_json::to_string(&bytecode).unwrap().as_bytes(),
                )?;
                fs::write(
                    PathBuf::from(format!("{file_path}/output/abi.json")),
                    serde_json::to_string(&abi).unwrap().as_bytes(),
                )?;
            }
        }

        project.rerun_if_sources_changed();
    }

    Ok(())
}
