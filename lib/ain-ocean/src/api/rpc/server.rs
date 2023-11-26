use crate::api::rpc::block::register_block_methods;
use crate::api::rpc::config::Config;
use jsonrpsee::server::ServerBuilder;
use jsonrpsee::RpcModule;
use tokio::signal;

#[derive(Clone, Debug)]
pub struct OceanRPCServer {
    pub rpc_config: Config,
}

impl OceanRPCServer {
    pub async fn start_rpc_server(&self) -> anyhow::Result<()> {
        let listen = self.rpc_config.listen.clone();
        let server = ServerBuilder::default().build(listen).await?;
        let mut block_module = RpcModule::new(());
        register_block_methods(&mut block_module)?;
        let server_handle = server.start(block_module);
        // Wait for a Ctrl+C signal before shutting down
        signal::ctrl_c().await.expect("failed to listen for event");
        let _ = server_handle.stop();

        Ok(())
    }
}
