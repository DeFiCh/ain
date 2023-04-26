use std::str::FromStr;

#[derive(Debug)]
pub enum Format {
    Rust,
    Json,
    PrettyJson,
}

impl FromStr for Format {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "json" => Ok(Format::Json),
            "rust" => Ok(Format::Rust),
            "pp" => Ok(Format::PrettyJson),
            _ => Err(format!("Unsupported format: {}", s)),
        }
    }
}
