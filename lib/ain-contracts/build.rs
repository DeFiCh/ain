use std::{env, fs, path::PathBuf};

use anyhow::{bail, Context, Result};
use ethers_solc::{artifacts::Optimizer, Project, ProjectPathsConfig, Solc, SolcConfig};

fn main() -> Result<()> {
    let manifest_path = PathBuf::from(env::var("CARGO_MANIFEST_DIR")?);

    // We use CARGO_TARGET_DIR from Makefile instead of OUT_DIR.
    // Reason: Currently setting --out-dir is nightly only, so there's no way to get OUT_DIR
    // out of cargo reliably for pointing non cargo deps (eg: python test files) determinisitcally.
    let target_dir: PathBuf = PathBuf::from(env::var("CARGO_TARGET_DIR")?);
    let solc_artifact_dir = target_dir.join("sol_artifacts");

    // Solidity project root and contract names relative to our project
    let contracts = vec![
        ("dfi_reserved", "DFIReserved"),
        ("dfi_intrinsics_registry", "DFIIntrinsicsRegistry"),
        ("dfi_intrinsics_v1", "DFIIntrinsicsV1"),
        ("transfer_domain", "TransferDomain"),
        ("transfer_domain_v1", "TransferDomainV1"),
        ("dst20", "DST20"),
        ("dst20_v1", "DST20V1"),
        ("dst20_v2", "DST20V2"),
    ];

    for (sol_project_name, contract_name) in contracts {
        let solc = Solc::default();

        let sol_project_root = manifest_path.join(sol_project_name);
        if !sol_project_root.exists() {
            bail!("Solidity project missing: {sol_project_root:?}");
        }

        let paths = ProjectPathsConfig::builder()
            .root(&sol_project_root)
            .sources(&sol_project_root)
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

        let output = project.compile()?;
        let artifacts = output.into_artifacts();
        let sol_project_outdir = solc_artifact_dir.join(sol_project_name);

        for (id, artifact) in artifacts {
            if id.name != contract_name {
                continue;
            }

            let abi = artifact.abi.context("ABI not found")?;
            let bytecode = artifact.bytecode.context("Bytecode not found")?;
            let deployed_bytecode = artifact
                .deployed_bytecode
                .context("Deployed bytecode not found")?;

            let items = [
                ("abi.json", serde_json::to_string(&abi)?),
                ("bytecode.json", serde_json::to_string(&bytecode)?),
                (
                    "deployed_bytecode.json",
                    serde_json::to_string(&deployed_bytecode)?,
                ),
            ];

            fs::create_dir_all(&sol_project_outdir)?;
            for (file_name, contents) in items {
                fs::write(sol_project_outdir.join(file_name), contents.as_bytes())?;
            }
        }

        project.rerun_if_sources_changed();
    }
    Ok(())
}
