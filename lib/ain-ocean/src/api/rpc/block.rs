use anyhow::Result;
use jsonrpsee::RpcModule;

pub fn register_block_methods(module: &mut RpcModule<()>) -> Result<()> {
    module.register_method("block_getBlock", |_params, _| {
        // Implement your method logic here
        todo!()
    })?;

    module.register_method("block_submitBlock", |_params, _| {
        // Implement your method logic here
        todo!()
    })?;

    Ok(())
}
