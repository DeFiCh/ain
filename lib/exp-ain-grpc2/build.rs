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
    let serde_flatten_attr = "#[serde(flatten)]";

    prost_build
        .out_dir(gen_path)
        .enum_attribute(".", default_attrs)
        .field_attribute("status_or_info", serde_flatten_attr)
        .message_attribute(".", default_attrs)
        .compile_protos(
            &["proto/services.proto"],
            &[
                "proto",
                proto_include.to_str().ok_or(format_err!("path err"))?,
            ],
        )?;
    Ok(())
}
