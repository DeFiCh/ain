use crate::proto;
use async_trait::async_trait;
use tonic::{Request, Response, Status};

use proto::eth::{
    EthAccountsResponse, EthBlockInfo, EthBlockNumberResponse, EthCallRequest, EthCallResponse,
    EthChainIdResponse, EthGetBalanceRequest, EthGetBalanceResponse, EthGetBlockByHashRequest,
    EthGetBlockByNumberRequest, EthGetBlockTransactionCountByHashRequest,
    EthGetBlockTransactionCountByHashResponse, EthGetBlockTransactionCountByNumberRequest,
    EthGetBlockTransactionCountByNumberResponse, EthGetCodeRequest, EthGetCodeResponse,
    EthGetStorageAtRequest, EthGetStorageAtResponse, EthGetTransactionByBlockHashAndIndexRequest,
    EthGetTransactionByBlockNumberAndIndexRequest, EthGetTransactionByHashRequest,
    EthMiningResponse, EthSendRawTransactionRequest, EthSendRawTransactionResponse,
    EthSendTransactionRequest, EthSendTransactionResponse, EthTransactionInfo,
};

struct ApiImpl {}

#[async_trait]
impl proto::eth::api_server::Api for ApiImpl {
    async fn eth_accounts(
        &self,
        request: Request<()>,
    ) -> Result<Response<EthAccountsResponse>, Status> {
        todo!()
    }

    async fn eth_call(
        &self,
        request: Request<EthCallRequest>,
    ) -> Result<Response<EthCallResponse>, Status> {
        todo!()
    }

    async fn eth_get_balance(
        &self,
        request: Request<EthGetBalanceRequest>,
    ) -> Result<Response<EthGetBalanceResponse>, Status> {
        todo!()
    }

    async fn eth_get_block_by_hash(
        &self,
        request: Request<EthGetBlockByHashRequest>,
    ) -> Result<Response<EthBlockInfo>, Status> {
        todo!()
    }

    async fn eth_send_transaction(
        &self,
        request: Request<EthSendTransactionRequest>,
    ) -> Result<Response<EthSendTransactionResponse>, Status> {
        todo!()
    }

    async fn eth_chain_id(
        &self,
        request: Request<()>,
    ) -> Result<Response<EthChainIdResponse>, Status> {
        todo!()
    }

    async fn net_version(
        &self,
        request: Request<()>,
    ) -> Result<Response<EthChainIdResponse>, Status> {
        todo!()
    }

    async fn eth_block_number(
        &self,
        request: Request<()>,
    ) -> Result<Response<EthBlockNumberResponse>, Status> {
        todo!()
    }

    async fn eth_get_block_by_number(
        &self,
        request: Request<EthGetBlockByNumberRequest>,
    ) -> Result<Response<EthBlockInfo>, Status> {
        todo!()
    }

    async fn eth_get_transaction_by_hash(
        &self,
        request: Request<EthGetTransactionByHashRequest>,
    ) -> Result<Response<EthTransactionInfo>, Status> {
        todo!()
    }

    async fn eth_get_transaction_by_block_hash_and_index(
        &self,
        request: Request<EthGetTransactionByBlockHashAndIndexRequest>,
    ) -> Result<Response<EthTransactionInfo>, Status> {
        todo!()
    }

    async fn eth_get_transaction_by_block_number_and_index(
        &self,
        request: Request<EthGetTransactionByBlockNumberAndIndexRequest>,
    ) -> Result<Response<EthTransactionInfo>, Status> {
        todo!()
    }

    async fn eth_mining(
        &self,
        request: Request<()>,
    ) -> Result<Response<EthMiningResponse>, Status> {
        todo!()
    }

    async fn eth_get_block_transaction_count_by_hash(
        &self,
        request: Request<EthGetBlockTransactionCountByHashRequest>,
    ) -> Result<Response<EthGetBlockTransactionCountByHashResponse>, Status> {
        todo!()
    }

    async fn eth_get_block_transaction_count_by_number(
        &self,
        request: Request<EthGetBlockTransactionCountByNumberRequest>,
    ) -> Result<Response<EthGetBlockTransactionCountByNumberResponse>, Status> {
        todo!()
    }

    async fn eth_get_code(
        &self,
        request: Request<EthGetCodeRequest>,
    ) -> Result<Response<EthGetCodeResponse>, Status> {
        todo!()
    }

    async fn eth_get_storage_at(
        &self,
        request: Request<EthGetStorageAtRequest>,
    ) -> Result<Response<EthGetStorageAtResponse>, Status> {
        todo!()
    }

    async fn eth_send_raw_transaction(
        &self,
        request: Request<EthSendRawTransactionRequest>,
    ) -> Result<Response<EthSendRawTransactionResponse>, Status> {
        todo!()
    }
}
