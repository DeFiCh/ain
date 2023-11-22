use crate::config::Config as rpc_config;

#[derive(Clone, Debug)]
pub struct OceanRPCServer {
    pub config: rpc_config,
}

impl OceanRPCServer {
    fn new(config: rpc_config) -> Self {
        Self { config }
    }

    async fn start_server(&self) -> Result<(), Box<dyn std::error::Error>> {
        let addr = self.config.listen.to_string(); // Handle the error as needed
        let server = self.config.listen.to_string();

        Ok(())
    }
}
