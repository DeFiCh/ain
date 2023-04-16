use crate::block::{BlockNumber, RpcBlock};
use crate::codegen::types::{
    EthAccountsResult, EthBlockInfo, EthBlockNumberResult, EthCallInput, EthCallResult,
    EthChainIdResult, EthGetBalanceInput, EthGetBalanceResult, EthGetBlockByHashInput,
    EthGetBlockByNumberInput, EthGetBlockTransactionCountByHashInput,
    EthGetBlockTransactionCountByHashResult, EthGetBlockTransactionCountByNumberInput,
    EthGetBlockTransactionCountByNumberResult, EthGetCodeInput, EthGetCodeResult,
    EthGetStorageAtInput, EthGetStorageAtResult, EthGetTransactionByBlockHashAndIndexInput,
    EthGetTransactionByBlockNumberAndIndexInput, EthGetTransactionByHashInput, EthMiningResult,
    EthSendRawTransactionInput, EthSendRawTransactionResult, EthTransactionInfo,
};

use ain_cpp_imports::publish_eth_transaction;
use ain_evm::handler::Handlers;
use ethereum::{Block, BlockAny, PartialHeader, TransactionV2};
use jsonrpsee::proc_macros::rpc;
use primitive_types::{H160, H256, U256};
use std::convert::Into;
use std::sync::Arc;

#[rpc(server)]
pub trait MetachainRPC {
    #[method(name = "eth_call")]
    fn eth_call(&self, input: EthCallInput) -> Result<Vec<u8>, jsonrpsee::core::Error>;

    #[method(name = "eth_accounts")]
    fn eth_accounts(&self) -> Result<Vec<H160>, jsonrpsee::core::Error>;

    #[method(name = "eth_getBalance")]
    fn eth_getBalance(&self, address: H160) -> Result<U256, jsonrpsee::core::Error>;

    #[method(name = "eth_getBlockByHash")]
    fn eth_getBlockByHash(
        &self,
        input: EthGetBlockByHashInput,
    ) -> Result<BlockAny, jsonrpsee::core::Error>;

    #[method(name = "eth_chainId")]
    fn eth_chainId(&self) -> Result<String, jsonrpsee::core::Error>;

    #[method(name = "net_version")]
    fn net_version(&self) -> Result<String, jsonrpsee::core::Error>;

    #[method(name = "eth_blockNumber")]
    fn eth_blockNumber(&self) -> Result<U256, jsonrpsee::core::Error>;

    #[method(name = "eth_getBlockByNumber")]
    fn eth_getBlockByNumber(
        &self,
        block_number: BlockNumber,
        full_transaction: bool,
    ) -> Result<Option<RpcBlock>, jsonrpsee::core::Error>;

    #[method(name = "eth_mining")]
    fn eth_mining(&self) -> Result<bool, jsonrpsee::core::Error>;

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
    fn eth_getBlockTransactionCountByHash(
        &self,
        input: EthGetBlockTransactionCountByHashInput,
    ) -> Result<EthGetBlockTransactionCountByHashResult, jsonrpsee::core::Error>;

    #[method(name = "eth_getBlockTransactionCountByNumber")]
    fn eth_getBlockTransactionCountByNumber(
        &self,
        input: EthGetBlockTransactionCountByNumberInput,
    ) -> Result<EthGetBlockTransactionCountByNumberResult, jsonrpsee::core::Error>;

    #[method(name = "eth_getCode")]
    fn eth_getCode(
        &self,
        input: EthGetCodeInput,
    ) -> Result<EthGetCodeResult, jsonrpsee::core::Error>;

    #[method(name = "eth_getStorageAt")]
    fn eth_getStorageAt(
        &self,
        input: EthGetStorageAtInput,
    ) -> Result<EthGetStorageAtResult, jsonrpsee::core::Error>;

    #[method(name = "eth_sendRawTransaction")]
    fn eth_sendRawTransaction(
        &self,
        input: EthSendRawTransactionInput,
    ) -> Result<EthSendRawTransactionResult, jsonrpsee::core::Error>;
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
    fn eth_call(&self, input: EthCallInput) -> Result<Vec<u8>, jsonrpsee::core::Error> {
        let EthCallInput {
            transaction_info, ..
        } = input;
        let EthTransactionInfo {
            from,
            to,
            gas,
            value,
            data,
            ..
        } = transaction_info.expect("TransactionInfo is required");

        let from = from.parse().ok();
        let to = to.map(|addr| addr.parse::<H160>().expect("Wrong `to` address format"));
        let value: U256 = value.parse().expect("Wrong value format");

        let (_, data) = self
            .handler
            .evm
            .call(from, to, value, data.as_bytes(), gas, vec![]);

        Ok(data)
    }

    fn eth_accounts(&self) -> Result<Vec<H160>, jsonrpsee::core::Error> {
        // Get from wallet
        Ok(vec![])
    }

    fn eth_getBalance(&self, address: H160) -> Result<U256, jsonrpsee::core::Error> {
        Ok(self.handler.evm.get_balance(address))
    }

    fn eth_getBlockByHash(
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

    fn eth_chainId(&self) -> Result<String, jsonrpsee::core::Error> {
        let chain_id = ain_cpp_imports::get_chain_id().unwrap();

        Ok(format!("{:#x}", chain_id))
    }

    fn net_version(&self) -> Result<String, jsonrpsee::core::Error> {
        let chain_id = ain_cpp_imports::get_chain_id().unwrap();

        Ok(format!("{}", chain_id))
    }

    fn eth_blockNumber(&self) -> Result<U256, jsonrpsee::core::Error> {
        let count = self
            .handler
            .storage
            .get_latest_block()
            .map(|block| block.header.number)
            .unwrap_or_default();

        Ok(U256::from(1))
        // Ok(count)
    }

    fn eth_getBlockByNumber(
        &self,
        block_number: BlockNumber,
        full_transaction: bool,
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

    fn eth_mining(&self) -> Result<bool, jsonrpsee::core::Error> {
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

    fn eth_getBlockTransactionCountByHash(
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

    fn eth_getBlockTransactionCountByNumber(
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

    fn eth_getCode(
        &self,
        input: EthGetCodeInput,
    ) -> Result<EthGetCodeResult, jsonrpsee::core::Error> {
        let EthGetCodeInput { address, .. } = input;

        let address = address.parse().expect("Invalid address");
        let code = self.handler.evm.get_code(address);

        Ok(EthGetCodeResult {
            code: format!("{:#x?}", code),
        })
    }

    fn eth_getStorageAt(
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

    fn eth_sendRawTransaction(
        &self,
        input: EthSendRawTransactionInput,
    ) -> Result<EthSendRawTransactionResult, jsonrpsee::core::Error> {
        let EthSendRawTransactionInput { transaction } = input;

        let hex = hex::decode(transaction).expect("Invalid transaction");
        let stat = publish_eth_transaction(hex).unwrap();

        println!("{stat}");

        Ok(EthSendRawTransactionResult {
            hash: format!("{stat}"),
        })
    }
}
