use anyhow::Result;

fn main() -> Result<()> {
    std::env::set_var("PROTOC", protobuf_src::protoc());

    let mut prost_build = prost_build::Config::new();

    prost_build.compile_protos(
        &["proto/services.proto"],
        &["proto", &protobuf_src::include().to_string_lossy()],
    )?;
    Ok(())
}
