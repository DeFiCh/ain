use crate::config::Config as rpc_config;
use std::future;
use tarpc::serde_transport::tcp;
use tarpc::server::{self, Channel};

#[derive(Clone, Debug)]
pub struct OceanRPCServer {
    pub config: rpc_config,
}

impl OceanRPCServer {
    fn new(config: rpc_config) -> Self {
        Self { config }
    }

    async fn start_server(
        &self,
        rpc_listen_addr: String,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let addr = self.config.listen.to_string(); // Handle the error as needed
        let server = self.config.listen;
        let mut listener = tcp::listen(&server, Json::default).await?;
        listener.config_mut().max_frame_length(usize::MAX);
        listener
            .filter_map(|r| future::ready(r.ok()))
            .map(server::BaseChannel::with_defaults)
            .max_channels_per_key(1, |t| t.transport().peer_addr().unwrap().ip())
            .map(|channel| {
                // let server = HelloServer(channel.transport().peer_addr().unwrap());
                // channel.execute(server.serve())
            })
            // Max 10 channels.
            .buffer_unordered(10)
            .for_each(|_| async {})
            .await;

        Ok(())
    }
}
