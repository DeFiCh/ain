use anyhow::{format_err, Result};
use std::path::PathBuf;

fn main() -> Result<()> {
    std::env::set_var("PROTOC", protobuf_src::protoc());
    let proto_include = protobuf_src::include();

    let manifest_path = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR")?);
    let gen_path = manifest_path.join("gen");
    std::fs::create_dir_all(&gen_path)?;

    let mut prost_build = prost_build::Config::new();

    let default_attrs = r#"
    #[derive(Eq, serde::Serialize, serde::Deserialize)]
    #[serde(rename_all="camelCase")]
    "#;
    let _serde_flatten_attr = "#[serde(flatten)]";
    let serde_untagged_attr = "#[serde(untagged)]";

    prost_build
        .out_dir(gen_path)
        .enum_attribute(".", serde_untagged_attr)
        // .field_attribute("<.>", serde_flatten_attr)
        .type_attribute(".", default_attrs)
        .compile_protos(
            &["proto/services.proto"],
            &[
                "proto",
                proto_include.to_str().ok_or(format_err!("path err"))?,
            ],
        )?;
    Ok(())
}
