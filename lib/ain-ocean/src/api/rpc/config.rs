#[derive(Clone, Debug)]
pub struct Config {
    pub enable: bool,
    pub listen: String,
}
impl Default for Config {
    fn default() -> Self {
        Config {
            enable: true,
            listen: "127.0.0.1:3000".to_string(),
        }
    }
}
