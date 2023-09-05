use std::env;
use std::fs;
use std::path::PathBuf;

use anyhow::format_err;
use anyhow::{Result};
use ethers_solc::{Project, ProjectPathsConfig, Solc};

fn main() -> Result<()> {
    let out_dir: PathBuf = PathBuf::from(env::var("OUT_DIR")?);
    let solc_path_str = env::var("SOLC_PATH")?;

    // Solidity project root and contract names relative to our project
    let contracts = vec![
        ("dfi_intrinsics", "DFIIntrinsics"),
        ("dst20", "DST20"),
        ("system_reserved", "SystemReservedContract"),
    ];

    for (sol_project_name, contract_name) in contracts {
        let solc = Solc::new(solc_path_str.clone());

        let sol_project_root = PathBuf::from(sol_project_name);
        if !sol_project_root.exists() {
            return Err(format_err!("Solidity project missing: {sol_project_root:?}"));
        }

        let paths = ProjectPathsConfig::builder()
            .root(&sol_project_root)
            .sources(&sol_project_root)
            .build()?;

        let project = Project::builder()
            .solc(solc)
            .paths(paths)
            .set_auto_detect(true)
            .no_artifacts()
            .build()?;
        let output = project.compile().unwrap();
        let artifacts = output.into_artifacts();

        let sol_project_outdir = out_dir.join(sol_project_name);

        for (id, artifact) in artifacts {
            if id.name == contract_name {
                let abi = artifact.abi.ok_or_else(|| format_err!("ABI not found"))?;
                let bytecode = artifact
                    .deployed_bytecode
                    .ok_or_else(|| format_err!("Bytecode not found"))?;
                let bytecode_out_path = sol_project_outdir.clone().join("bytecode.json");
                let abi_out_path = sol_project_outdir.clone().join("abi.json");

                fs::create_dir_all(sol_project_outdir.clone())?;
                fs::write(
                    bytecode_out_path,
                    serde_json::to_string(&bytecode)?.as_bytes(),
                )?;
                fs::write(
                    abi_out_path,
                    serde_json::to_string(&abi)?.as_bytes(),
                )?;
            }
        }

        project.rerun_if_sources_changed();
    }

    Ok(())
}
