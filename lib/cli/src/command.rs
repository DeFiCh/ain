use crate::{result::RpcResult, MetachainCLI};

use ain_grpc::rpc::MetachainRPCClient;
use jsonrpsee::http_client::HttpClient;

pub async fn execute_cli_command(
    cmd: MetachainCLI,
    client: &HttpClient,
) -> Result<RpcResult, jsonrpsee::core::Error> {
    let result = match cmd {
        MetachainCLI::Accounts => client.accounts().await?.into(),
        MetachainCLI::ChainId => client.chain_id().await?.into(),
        MetachainCLI::NetVersion => client.net_version().await?.into(),
        MetachainCLI::Mining => client.mining().await?.into(),
        MetachainCLI::Call { input } => client.call((*input).into()).await?.into(),
        MetachainCLI::GetBalance {
            address,
            block_number,
        } => client.get_balance(address, block_number).await?.into(),
        MetachainCLI::GetBlockByHash { hash } => client.get_block_by_hash(hash).await?.into(),
        MetachainCLI::HashRate => client.hash_rate().await?.into(),
        MetachainCLI::BlockNumber => client.block_number().await?.into(),
        MetachainCLI::GetBlockByNumber {
            block_number,
            full_transaction,
        } => client
            .get_block_by_number(block_number, full_transaction)
            .await?
            .into(),
        MetachainCLI::GetTransactionByHash { hash } => {
            client.get_transaction_by_hash(hash).await?.into()
        }
        MetachainCLI::GetTransactionByBlockHashAndIndex { hash, index } => client
            .get_transaction_by_block_hash_and_index(hash, index)
            .await?
            .into(),
        MetachainCLI::GetTransactionByBlockNumberAndIndex {
            block_number,
            index,
        } => client
            .get_transaction_by_block_number_and_index(block_number, index)
            .await?
            .into(),
        MetachainCLI::GetBlockTransactionCountByHash { hash } => client
            .get_block_transaction_count_by_hash(hash)
            .await?
            .into(),
        MetachainCLI::GetBlockTransactionCountByNumber { number } => client
            .get_block_transaction_count_by_number(number)
            .await?
            .into(),
        MetachainCLI::GetCode {
            address,
            block_number,
        } => client.get_code(address, block_number).await?.into(),
        MetachainCLI::GetStorageAt {
            address,
            position,
            block_number,
        } => client
            .get_storage_at(address, position, block_number)
            .await?
            .into(),
        MetachainCLI::SendRawTransaction { input } => {
            client.send_raw_transaction(&input).await?.into()
        }
        MetachainCLI::GetTransactionCount {
            input,
            block_number,
        } => client
            .get_transaction_count(input, block_number)
            .await?
            .into(),
        MetachainCLI::EstimateGas { input } => client.estimate_gas((*input).into()).await?.into(),
        MetachainCLI::GasPrice => client.gas_price().await?.into(),
    };
    Ok(result)
}
