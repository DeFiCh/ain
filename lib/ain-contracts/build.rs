use std::{env, fs, path::PathBuf};

use anyhow::format_err;
use ethers_solc::artifacts::Optimizer;
use ethers_solc::{Project, ProjectPathsConfig, Solc, SolcConfig};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // compile solidity project
    // configure `root` as our project root
    let contracts = vec![
        ("dfi_intrinsics", "DFIIntrinsics"),
        ("dst20", "DST20"),
        ("system_reserved", "SystemReservedContract"),
        ("transfer_domain", "TransferDomain"),
    ];

    for (file_path, contract_name) in contracts {
        let solc = Solc::new(env::var("SOLC_PATH")?);
        let output_path = env::var("CARGO_TARGET_DIR")?;
        let root = PathBuf::from(file_path);
        if !root.exists() {
            return Err("Project root {root:?} does not exists!".into());
        }

        let paths = ProjectPathsConfig::builder()
            .root(&root)
            .sources(&root)
            .build()?;

        let mut solc_config = SolcConfig::builder().build();

        solc_config.settings.optimizer = Optimizer {
            enabled: Some(true),
            runs: Some(u32::MAX as usize),
            details: None,
        };

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
                let deployed_bytecode = artifact
                    .deployed_bytecode
                    .expect("No deployed_bytecode found");

                let bytecode = artifact.bytecode.expect("No bytecode found");

                fs::create_dir_all(format!("{output_path}/ain_contracts/{file_path}"))?;
                fs::write(
                    PathBuf::from(format!(
                        "{output_path}/ain_contracts/{file_path}/bytecode.json"
                    )),
                    serde_json::to_string(&deployed_bytecode)
                        .unwrap()
                        .as_bytes(),
                )?;
                fs::write(
                    PathBuf::from(format!(
                        "{output_path}/ain_contracts/{file_path}/input.json"
                    )),
                    serde_json::to_string(&bytecode).unwrap().as_bytes(),
                )?;
                fs::write(
                    PathBuf::from(format!("{output_path}/ain_contracts/{file_path}/abi.json")),
                    serde_json::to_string(&abi).unwrap().as_bytes(),
                )?;
            }
        }

        project.rerun_if_sources_changed();
    }

    Ok(())
}
