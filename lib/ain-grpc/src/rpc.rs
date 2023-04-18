use crate::block::{BlockNumber, RpcBlock};
use crate::call_request::CallRequest;
use crate::codegen::types::{
    EthGetBlockByHashInput, EthGetBlockTransactionCountByHashInput,
    EthGetBlockTransactionCountByHashResult, EthGetBlockTransactionCountByNumberInput,
    EthGetBlockTransactionCountByNumberResult, EthGetStorageAtInput, EthGetStorageAtResult,
    EthTransactionInfo,
};

use ain_cpp_imports::publish_eth_transaction;
use ain_evm::evm::EVMState;
use ain_evm::handler::Handlers;
use ain_evm::transaction::SignedTx;
use ethereum::{Block, PartialHeader};
use jsonrpsee::proc_macros::rpc;
use primitive_types::{H160, H256, U256};
use std::convert::Into;
use std::sync::Arc;

type Result<T> = std::result::Result<T, jsonrpsee::core::Error>;

#[rpc(server, client)]
pub trait MetachainRPC {
    #[method(name = "eth_call")]
    fn call(&self, input: CallRequest) -> Result<String>;

    #[method(name = "eth_accounts")]
    fn accounts(&self) -> Result<Vec<H160>>;

    #[method(name = "eth_getBalance")]
    fn get_balance(&self, address: H160) -> Result<U256>;

    #[method(name = "eth_getBlockByHash")]
    fn get_block_by_hash(&self, input: EthGetBlockByHashInput) -> Result<Option<RpcBlock>>;

    #[method(name = "eth_chainId")]
    fn chain_id(&self) -> Result<String>;

    #[method(name = "eth_hashrate")]
    fn hash_rate(&self) -> Result<String, jsonrpsee::core::Error>;

    #[method(name = "net_version")]
    fn net_version(&self) -> Result<String>;

    #[method(name = "eth_blockNumber")]
    fn block_number(&self) -> Result<U256>;

    #[method(name = "eth_getBlockByNumber")]
    fn get_block_by_number(
        &self,
        block_number: BlockNumber,
        full_transaction: bool,
    ) -> Result<Option<RpcBlock>>;

    #[method(name = "eth_mining")]
    fn mining(&self) -> Result<bool>;

    #[method(name = "eth_getTransactionByHash")]
    fn get_transaction_by_hash(&self, hash: H256) -> Result<EthTransactionInfo>;

    #[method(name = "eth_getTransactionByBlockHashAndIndex")]
    fn get_transaction_by_block_hash_and_index(
        &self,
        hash: H256,
        index: usize,
    ) -> Result<EthTransactionInfo>;

    #[method(name = "eth_getTransactionByBlockNumberAndIndex")]
    fn get_transaction_by_block_number_and_index(
        &self,
        block_number: U256,
        index: usize,
    ) -> Result<EthTransactionInfo>;

    #[method(name = "eth_getBlockTransactionCountByHash")]
    fn get_block_transaction_count_by_hash(
        &self,
        input: EthGetBlockTransactionCountByHashInput,
    ) -> Result<EthGetBlockTransactionCountByHashResult>;

    #[method(name = "eth_getBlockTransactionCountByNumber")]
    fn get_block_transaction_count_by_number(
        &self,
        input: EthGetBlockTransactionCountByNumberInput,
    ) -> Result<EthGetBlockTransactionCountByNumberResult>;

    #[method(name = "eth_getCode")]
    fn get_code(&self, address: H160) -> Result<String>;

    #[method(name = "eth_getStorageAt")]
    fn get_storage_at(&self, input: EthGetStorageAtInput) -> Result<EthGetStorageAtResult>;

    #[method(name = "eth_sendRawTransaction")]
    fn send_raw_transaction(&self, input: &str) -> Result<String>;

    #[method(name = "eth_getTransactionCount")]
    fn get_transaction_count(&self, input: String) -> Result<String>;

    #[method(name = "eth_estimateGas")]
    fn estimate_gas(&self) -> Result<String>;

    #[method(name = "mc_getState")]
    fn get_state(&self) -> Result<EVMState>;
}

pub struct MetachainRPCModule {
    handler: Arc<Handlers>,
}

impl MetachainRPCModule {
    pub fn new(handler: Arc<Handlers>) -> Self {
        Self { handler }
    }
}

impl MetachainRPCServer for MetachainRPCModule {
    fn call(&self, input: CallRequest) -> Result<String> {
        let CallRequest {
            from,
            to,
            gas,
            value,
            data,
            ..
        } = input;
        let (_, data) = self.handler.evm.call(
            from,
            to,
            value.unwrap_or_default(),
            &data.unwrap_or_default(),
            gas.unwrap_or_default().as_u64(),
            vec![],
        );

        Ok(hex::encode(data))
    }

    fn accounts(&self) -> Result<Vec<H160>> {
        // Get from wallet
        Ok(vec![])
    }

    fn get_balance(&self, address: H160) -> Result<U256> {
        Ok(self.handler.evm.get_balance(address))
    }

    fn get_block_by_hash(&self, input: EthGetBlockByHashInput) -> Result<Option<RpcBlock>> {
        let EthGetBlockByHashInput { hash, .. } = input;

        let hash: H256 = hash.parse().expect("Invalid hash");

        Ok(self
            .handler
            .storage
            .get_block_by_hash(&hash)
            .map(Into::into))
    }

    fn chain_id(&self) -> Result<String> {
        let chain_id = ain_cpp_imports::get_chain_id().unwrap();

        Ok(format!("{:#x}", chain_id))
    }

    fn hash_rate(&self) -> Result<String> {
        Ok("0x0".parse().unwrap())
    }

    fn net_version(&self) -> Result<String> {
        let chain_id = ain_cpp_imports::get_chain_id().unwrap();

        Ok(format!("{}", chain_id))
    }

    fn block_number(&self) -> Result<U256> {
        let count = self
            .handler
            .storage
            .get_latest_block()
            .map(|block| block.header.number)
            .unwrap_or_default();

        Ok(count)
    }

    fn get_block_by_number(
        &self,
        block_number: BlockNumber,
        _full_transaction: bool,
    ) -> Result<Option<RpcBlock>> {
        match block_number {
            BlockNumber::Num(number) => {
                println!("Getting bblock by number : {}", number);
                let number = U256::from(number);
                Ok(Some(
                    self.handler
                        .storage
                        .get_block_by_number(&number)
                        .unwrap_or_else(|| {
                            Block::new(
                                PartialHeader {
                                    parent_hash: Default::default(),
                                    beneficiary: Default::default(),
                                    state_root: Default::default(),
                                    receipts_root: Default::default(),
                                    logs_bloom: Default::default(),
                                    difficulty: Default::default(),
                                    number,
                                    gas_limit: Default::default(),
                                    gas_used: Default::default(),
                                    timestamp: Default::default(),
                                    extra_data: Default::default(),
                                    mix_hash: Default::default(),
                                    nonce: Default::default(),
                                },
                                Vec::new(),
                                Vec::new(),
                            )
                        })
                        .into(),
                ))
            }
            _ => Ok(None),
        }
    }

    fn mining(&self) -> Result<bool> {
        ain_cpp_imports::is_mining().map_err(|e| jsonrpsee::core::Error::Custom(e.to_string()))
    }

    fn get_transaction_by_hash(&self, hash: H256) -> Result<EthTransactionInfo> {
        self.handler
            .storage
            .get_transaction_by_hash(hash)
            .and_then(|tx| tx.try_into().ok())
            .ok_or(jsonrpsee::core::Error::Custom(String::from(
                "Missing transaction",
            )))
    }

    fn get_transaction_by_block_hash_and_index(
        &self,
        hash: H256,
        index: usize,
    ) -> Result<EthTransactionInfo> {
        self.handler
            .storage
            .get_transaction_by_block_hash_and_index(hash, index)
            .and_then(|tx| tx.try_into().ok())
            .ok_or(jsonrpsee::core::Error::Custom(String::from(
                "Missing transaction",
            )))
    }

    fn get_transaction_by_block_number_and_index(
        &self,
        number: U256,
        index: usize,
    ) -> Result<EthTransactionInfo> {
        self.handler
            .storage
            .get_transaction_by_block_number_and_index(number, index)
            .and_then(|tx| tx.try_into().ok())
            .ok_or(jsonrpsee::core::Error::Custom(String::from(
                "Missing transaction",
            )))
    }

    fn get_block_transaction_count_by_hash(
        &self,
        input: EthGetBlockTransactionCountByHashInput,
    ) -> Result<EthGetBlockTransactionCountByHashResult> {
        let EthGetBlockTransactionCountByHashInput { block_hash } = input;

        let block_hash = block_hash.parse().expect("Invalid hash");
        let block = self.handler.block.get_block_by_hash(block_hash).unwrap();
        let count = block.transactions.len();

        Ok(EthGetBlockTransactionCountByHashResult {
            number_transaction: format!("{:#x}", count),
        })
    }

    fn get_block_transaction_count_by_number(
        &self,
        input: EthGetBlockTransactionCountByNumberInput,
    ) -> Result<EthGetBlockTransactionCountByNumberResult> {
        let EthGetBlockTransactionCountByNumberInput { block_number } = input;

        let number: usize = block_number.parse().ok().unwrap();
        let block = self.handler.block.get_block_by_number(number).unwrap();
        let count = block.transactions.len();

        Ok(EthGetBlockTransactionCountByNumberResult {
            number_transaction: format!("{:#x}", count),
        })
    }

    fn get_code(&self, address: H160) -> Result<String> {
        let code = self.handler.evm.get_code(address);

        if code.is_empty() {
            return Ok(String::from("0x"));
        }

        Ok(format!("{:#x?}", code))
    }

    fn get_storage_at(&self, input: EthGetStorageAtInput) -> Result<EthGetStorageAtResult> {
        let EthGetStorageAtInput {
            address, position, ..
        } = input;

        let address = address.parse().expect("Invalid address");
        let position = position.parse().expect("Invalid postition");

        let storage = self.handler.evm.get_storage(address);
        let &value = storage.get(&position).unwrap_or(&H256::zero());

        Ok(EthGetStorageAtResult {
            value: format!("{:#x}", value),
        })
    }

    fn send_raw_transaction(&self, input: &str) -> Result<String> {
        println!("input : {:#?}", input);
        let raw_tx = input.strip_prefix("0x").unwrap_or(input);
        let hex = hex::decode(raw_tx).expect("Invalid transaction");
        let stat = publish_eth_transaction(hex).unwrap();

        let signed_tx = SignedTx::try_from(raw_tx).expect("Invalid hex");
        println!("{stat}");

        Ok(format!("{:#x}", signed_tx.transaction.hash()))
    }

    fn get_transaction_count(&self, input: String) -> Result<String> {
        let input = input.parse().expect("Invalid address");
        let nonce = self.handler.evm.get_nonce(input);

        Ok(format!("{:#x}", nonce))
    }

    fn estimate_gas(&self) -> Result<String> {
        Ok(format!("{:#x}", 21000))
    }

    fn get_state(&self) -> Result<EVMState> {
        Ok(self.handler.evm.state.read().unwrap().clone())
    }
}
