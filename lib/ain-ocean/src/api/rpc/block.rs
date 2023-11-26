use anyhow::Result;
use jsonrpsee::RpcModule;

pub fn register_block_methods(module: &mut RpcModule<()>) -> Result<()> {
    module.register_method("block_getBlock", |params, _| {
        // Implement your method logic here
        todo!()
    })?;

    module.register_method("block_submitBlock", |params, _| {
        // Implement your method logic here
        todo!()
    })?;

    Ok(())
}
