use std::env;
use std::fs;
use std::path::PathBuf;

use anyhow::format_err;
use ethers_solc::artifacts::output_selection::OutputSelection;
use ethers_solc::artifacts::{Optimizer, Settings};
use ethers_solc::{Project, ProjectPathsConfig, Solc, SolcConfig};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // compile solidity project
    // configure `root` as our project root
    let contracts = vec![
        ("dfi_intrinsics", "DFIIntrinsics"),
        ("dst20", "DST20"),
        ("system_reserved", "SystemReservedContract"),
    ];

    for (file_path, contract_name) in contracts {
        let solc = Solc::new(env::var("SOLC_PATH")?);
        let root = PathBuf::from(file_path);
        if !root.exists() {
            return Err("Project root {root:?} does not exists!".into());
        }

        let paths = ProjectPathsConfig::builder()
            .root(&root)
            .sources(&root)
            .build()?;

        let solc_config = SolcConfig::builder()
            .settings(Settings {
                stop_after: None,
                remappings: vec![],
                optimizer: Optimizer {
                    enabled: Some(true),
                    runs: Some(10000),
                    details: None,
                },
                model_checker: None,
                metadata: None,
                output_selection: OutputSelection::default_output_selection(),
                evm_version: None,
                via_ir: None,
                debug: None,
                libraries: Default::default(),
            })
            .build();

        let project = Project::builder()
            .solc(solc)
            .solc_config(solc_config)
            .paths(paths)
            .set_auto_detect(true)
            .no_artifacts()
            .build()?;
        let output = project.compile().unwrap();
        let artifacts = output.into_artifacts();

        for (id, artifact) in artifacts {
            if id.name == contract_name {
                let abi = artifact.abi.ok_or_else(|| format_err!("ABI not found"))?;
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
