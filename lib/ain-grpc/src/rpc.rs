use crate::block::{BlockNumber, RpcBlock};
use crate::codegen::types::{
    EthCallInput, EthGetBlockByHashInput, EthGetBlockTransactionCountByHashInput,
    EthGetBlockTransactionCountByHashResult, EthGetBlockTransactionCountByNumberInput,
    EthGetBlockTransactionCountByNumberResult, EthGetCodeInput, EthGetCodeResult,
    EthGetStorageAtInput, EthGetStorageAtResult, EthSendRawTransactionInput,
    EthSendRawTransactionResult, EthTransactionInfo,
};

use ain_cpp_imports::publish_eth_transaction;
use ain_evm::handler::Handlers;
use ethereum::{Block, BlockAny, PartialHeader};
use jsonrpsee::proc_macros::rpc;
use primitive_types::{H160, H256, U256};
use std::convert::Into;
use std::sync::Arc;

#[rpc(server)]
pub trait MetachainRPC {
    #[method(name = "eth_call")]
    fn call(&self, input: EthTransactionInfo) -> Result<Vec<u8>, jsonrpsee::core::Error>;

    #[method(name = "eth_accounts")]
    fn accounts(&self) -> Result<Vec<H160>, jsonrpsee::core::Error>;

    #[method(name = "eth_getBalance")]
    fn get_balance(&self, address: H160) -> Result<U256, jsonrpsee::core::Error>;

    #[method(name = "eth_getBlockByHash")]
    fn get_block_by_hash(
        &self,
        input: EthGetBlockByHashInput,
    ) -> Result<BlockAny, jsonrpsee::core::Error>;

    #[method(name = "eth_chainId")]
    fn chain_id(&self) -> Result<String, jsonrpsee::core::Error>;

    #[method(name = "net_version")]
    fn net_version(&self) -> Result<String, jsonrpsee::core::Error>;

    #[method(name = "eth_blockNumber")]
    fn block_number(&self) -> Result<U256, jsonrpsee::core::Error>;

    #[method(name = "eth_getBlockByNumber")]
    fn get_block_by_number(
        &self,
        block_number: BlockNumber,
        full_transaction: bool,
    ) -> Result<Option<RpcBlock>, jsonrpsee::core::Error>;

    #[method(name = "eth_mining")]
    fn mining(&self) -> Result<bool, jsonrpsee::core::Error>;

    // #[method(name = "eth_getTransactionByHash")]
    // fn eth_GetTransactionByHash(
    //     &self,
    //     input: EthGetTransactionByHashInput,
    // ) -> Result<EthTransactionInfo, jsonrpsee::core::Error>;

    // #[method(name = "eth_getTransactionByBlockHashAndIndex")]
    // fn eth_GetTransactionByBlockHashAndIndex(
    //     &self,
    //     input: EthGetTransactionByBlockHashAndIndexInput,
    // ) -> Result<EthTransactionInfo, jsonrpsee::core::Error>;

    // #[method(name = "eth_getTransactionByBlockNumberAndIndex")]
    // fn eth_GetTransactionByBlockNumberAndIndex(
    //     &self,
    //     input: EthGetTransactionByBlockNumberAndIndexInput,
    // ) -> Result<EthTransactionInfo, jsonrpsee::core::Error>;

    #[method(name = "eth_getBlockTransactionCountByHash")]
    fn get_block_transaction_count_by_hash(
        &self,
        input: EthGetBlockTransactionCountByHashInput,
    ) -> Result<EthGetBlockTransactionCountByHashResult, jsonrpsee::core::Error>;

    #[method(name = "eth_getBlockTransactionCountByNumber")]
    fn get_block_transaction_count_by_number(
        &self,
        input: EthGetBlockTransactionCountByNumberInput,
    ) -> Result<EthGetBlockTransactionCountByNumberResult, jsonrpsee::core::Error>;

    #[method(name = "eth_getCode")]
    fn get_code(&self, address: String) -> Result<String, jsonrpsee::core::Error>;

    #[method(name = "eth_getStorageAt")]
    fn get_storage_at(
        &self,
        input: EthGetStorageAtInput,
    ) -> Result<EthGetStorageAtResult, jsonrpsee::core::Error>;

    #[method(name = "eth_sendRawTransaction")]
    fn send_raw_transaction(
        &self,
        input: String,
    ) -> Result<EthSendRawTransactionResult, jsonrpsee::core::Error>;

    #[method(name = "eth_getTransactionCount")]
    fn get_transaction_count(&self, input: String) -> Result<String, jsonrpsee::core::Error>;

    #[method(name = "eth_estimateGas")]
    fn estimate_gas(&self) -> Result<String, jsonrpsee::core::Error>;
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
    fn call(&self, input: EthTransactionInfo) -> Result<Vec<u8>, jsonrpsee::core::Error> {
        let EthTransactionInfo {
            from,
            to,
            gas,
            value,
            data,
            ..
        } = input;

        let from = from.map(|addr| addr.parse::<H160>().expect("Wrong `from` address format"));
        let to = to.map(|addr| addr.parse::<H160>().expect("Wrong `to` address format"));
        let value: U256 = value
            .map(|addr| addr.parse::<U256>().expect("Wrong `value` address format"))
            .unwrap_or_default();
        let gas: u64 = gas.unwrap_or_default();

        let (_, data) = self
            .handler
            .evm
            .call(from, to, value, data.as_bytes(), gas, vec![]);

        Ok(Hex::encode(data))
    }

    fn accounts(&self) -> Result<Vec<H160>, jsonrpsee::core::Error> {
        // Get from wallet
        Ok(vec![])
    }

    fn get_balance(&self, address: H160) -> Result<U256, jsonrpsee::core::Error> {
        Ok(self.handler.evm.get_balance(address))
    }

    fn get_block_by_hash(
        &self,
        input: EthGetBlockByHashInput,
    ) -> Result<BlockAny, jsonrpsee::core::Error> {
        let EthGetBlockByHashInput { hash, .. } = input;

        let hash: H256 = hash.parse().expect("Invalid hash");

        self.handler
            .storage
            .get_block_by_hash(&hash)
            // .map(Into::into)
            .ok_or(jsonrpsee::core::Error::Custom(String::from(
                "Missing block",
            )))
    }

    fn chain_id(&self) -> Result<String, jsonrpsee::core::Error> {
        let chain_id = ain_cpp_imports::get_chain_id().unwrap();

        Ok(format!("{:#x}", chain_id))
    }

    fn net_version(&self) -> Result<String, jsonrpsee::core::Error> {
        let chain_id = ain_cpp_imports::get_chain_id().unwrap();

        Ok(format!("{}", chain_id))
    }

    fn block_number(&self) -> Result<U256, jsonrpsee::core::Error> {
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
    ) -> Result<Option<RpcBlock>, jsonrpsee::core::Error> {
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
                                    number: U256::from(number),
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

    fn mining(&self) -> Result<bool, jsonrpsee::core::Error> {
        ain_cpp_imports::is_mining()
            .map_err(|e| jsonrpsee::core::Error::Custom(String::from(e.to_string())))
    }

    // fn eth_GetTransactionByHash(
    //     &self,
    //     input: EthGetTransactionByHashInput,
    // ) -> Result<EthTransactionInfo, jsonrpsee::core::Error> {
    //     let hash: H256 = input.hash.parse().ok().unwrap();
    //     self.handler
    //         .storage
    //         .get_transaction_by_hash(hash)
    //         .and_then(|tx| tx.try_into().ok())
    //         .ok_or(jsonrpsee::core::Error::Custom(String::from(
    //             "Missing transaction",
    //         )))
    // }

    // fn eth_GetTransactionByBlockHashAndIndex(
    //     &self,
    //     input: EthGetTransactionByBlockHashAndIndexInput,
    // ) -> Result<EthTransactionInfo, jsonrpsee::core::Error> {
    //     let hash: H256 = input.block_hash.parse().ok().unwrap();
    //     let index: usize = input.index.parse().ok().unwrap();
    //     self.handler
    //         .storage
    //         .get_transaction_by_block_hash_and_index(hash, index)
    //         .and_then(|tx| tx.try_into().ok())
    //         .ok_or(jsonrpsee::core::Error::Custom(String::from(
    //             "Missing transaction",
    //         )))
    // }

    // fn eth_GetTransactionByBlockNumberAndIndex(
    //     &self,
    //     input: EthGetTransactionByBlockNumberAndIndexInput,
    // ) -> Result<EthTransactionInfo, jsonrpsee::core::Error> {
    //     let number: U256 = input.block_number.parse().ok().unwrap();
    //     let index: usize = input.index.parse().ok().unwrap();
    //     self.handler
    //         .storage
    //         .get_transaction_by_block_number_and_index(number, index)
    //         .and_then(|tx| tx.try_into().ok())
    //         .ok_or(jsonrpsee::core::Error::Custom(String::from(
    //             "Missing transaction",
    //         )))
    // }

    fn get_block_transaction_count_by_hash(
        &self,
        input: EthGetBlockTransactionCountByHashInput,
    ) -> Result<EthGetBlockTransactionCountByHashResult, jsonrpsee::core::Error> {
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
    ) -> Result<EthGetBlockTransactionCountByNumberResult, jsonrpsee::core::Error> {
        let EthGetBlockTransactionCountByNumberInput { block_number } = input;

        let number: usize = block_number.parse().ok().unwrap();
        let block = self.handler.block.get_block_by_number(number).unwrap();
        let count = block.transactions.len();

        Ok(EthGetBlockTransactionCountByNumberResult {
            number_transaction: format!("{:#x}", count),
        })
    }

    fn get_code(&self, address: String) -> Result<String, jsonrpsee::core::Error> {
        let address = address.parse().expect("Invalid address");
        let code = self.handler.evm.get_code(address);

        if code.len() == 0 {
            return Ok(format!("0x"));
        }

        Ok(format!("{:#x?}", code))
    }

    fn get_storage_at(
        &self,
        input: EthGetStorageAtInput,
    ) -> Result<EthGetStorageAtResult, jsonrpsee::core::Error> {
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

    fn send_raw_transaction(
        &self,
        input: String,
    ) -> Result<EthSendRawTransactionResult, jsonrpsee::core::Error> {
        let hex = hex::decode(input).expect("Invalid transaction");
        let stat = publish_eth_transaction(hex).unwrap();

        println!("{stat}");

        Ok(EthSendRawTransactionResult {
            hash: format!("{stat}"),
        })
    }

    fn get_transaction_count(&self, input: String) -> Result<String, jsonrpsee::core::Error> {
        let input = input.parse().expect("Invalid address");
        let nonce = self.handler.evm.get_nonce(input);

        Ok(format!("{:#x}", nonce))
    }

    fn estimate_gas(&self) -> Result<String, jsonrpsee::core::Error> {
        Ok(format!("{:#x}", 21000))
    }
}
