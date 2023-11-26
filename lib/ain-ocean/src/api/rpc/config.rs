#[derive(Clone, Debug)]
pub struct Config {
    pub enable: bool,
    pub listen: String,
}

impl Config {
    fn default() -> Config {
        Config {
            enable: false,
            listen: "3000".to_string(),
        }
    }
}
