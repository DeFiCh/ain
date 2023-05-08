use crate::block::{BlockNumber, RpcBlock};
use crate::call_request::CallRequest;
use crate::codegen::types::{EthPendingTransactionInfo, EthTransactionInfo};

use crate::receipt::ReceiptResult;
// use ain_evm::evm::EVMState;
use ain_evm::executor::TxResponse;
use ain_evm::handler::Handlers;

use ain_evm::storage::traits::{BlockStorage, ReceiptStorage, TransactionStorage};
use ain_evm::transaction::{SignedTx, TransactionError};
use jsonrpsee::core::{Error, RpcResult};
use jsonrpsee::proc_macros::rpc;
use log::debug;
use primitive_types::{H160, H256, U256};
use std::convert::Into;
use std::sync::Arc;

#[rpc(server, client)]
pub trait MetachainRPC {
    #[method(name = "eth_call")]
    fn call(&self, input: CallRequest) -> RpcResult<String>;

    #[method(name = "eth_accounts")]
    fn accounts(&self) -> RpcResult<Vec<String>>;

    #[method(name = "eth_getBalance")]
    fn get_balance(&self, address: H160, block_number: Option<BlockNumber>) -> RpcResult<U256>;

    #[method(name = "eth_getBlockByHash")]
    fn get_block_by_hash(&self, hash: H256) -> RpcResult<Option<RpcBlock>>;

    #[method(name = "eth_chainId")]
    fn chain_id(&self) -> RpcResult<String>;

    #[method(name = "eth_hashrate")]
    fn hash_rate(&self) -> RpcResult<String>;

    #[method(name = "net_version")]
    fn net_version(&self) -> RpcResult<String>;

    #[method(name = "eth_blockNumber")]
    fn block_number(&self) -> RpcResult<U256>;

    #[method(name = "eth_getBlockByNumber")]
    fn get_block_by_number(
        &self,
        block_number: BlockNumber,
        full_transaction: bool,
    ) -> RpcResult<Option<RpcBlock>>;

    #[method(name = "eth_mining")]
    fn mining(&self) -> RpcResult<bool>;

    #[method(name = "eth_getTransactionByHash")]
    fn get_transaction_by_hash(&self, hash: H256) -> RpcResult<Option<EthTransactionInfo>>;

    #[method(name = "eth_getTransactionByBlockHashAndIndex")]
    fn get_transaction_by_block_hash_and_index(
        &self,
        hash: H256,
        index: usize,
    ) -> RpcResult<Option<EthTransactionInfo>>;

    #[method(name = "eth_getTransactionByBlockNumberAndIndex")]
    fn get_transaction_by_block_number_and_index(
        &self,
        block_number: U256,
        index: usize,
    ) -> RpcResult<Option<EthTransactionInfo>>;

    #[method(name = "eth_getBlockTransactionCountByHash")]
    fn get_block_transaction_count_by_hash(&self, hash: H256) -> RpcResult<usize>;

    #[method(name = "eth_getBlockTransactionCountByNumber")]
    fn get_block_transaction_count_by_number(&self, number: BlockNumber) -> RpcResult<usize>;

    #[method(name = "eth_pendingTransactions")]
    fn get_pending_transaction(&self) -> RpcResult<Vec<EthPendingTransactionInfo>>;

    #[method(name = "eth_getCode")]
    fn get_code(&self, address: H160, block_number: Option<BlockNumber>) -> RpcResult<String>;

    #[method(name = "eth_getStorageAt")]
    fn get_storage_at(
        &self,
        address: H160,
        position: U256,
        block_number: Option<BlockNumber>,
    ) -> RpcResult<H256>;

    #[method(name = "eth_sendRawTransaction")]
    fn send_raw_transaction(&self, tx: &str) -> RpcResult<String>;

    #[method(name = "eth_getTransactionCount")]
    fn get_transaction_count(
        &self,
        address: H160,
        block_number: Option<BlockNumber>,
    ) -> RpcResult<String>;

    #[method(name = "eth_estimateGas")]
    fn estimate_gas(&self, input: CallRequest) -> RpcResult<String>;

    #[method(name = "eth_gasPrice")]
    fn gas_price(&self) -> RpcResult<String>;

    #[method(name = "eth_getTransactionReceipt")]
    fn get_receipt(&self, hash: H256) -> RpcResult<Option<ReceiptResult>>;

    #[method(name = "eth_getWork")]
    fn get_getwork(&self) -> RpcResult<Vec<String>>;

    #[method(name = "eth_submitWork")]
    fn eth_submitwork(&self, nonce: String, hash: String, digest: String) -> RpcResult<bool>;

    #[method(name = "eth_submitHashrate")]
    fn eth_submithashrate(&self, hashrate: String, id: String) -> RpcResult<bool>;
}

pub struct MetachainRPCModule {
    handler: Arc<Handlers>,
}

impl MetachainRPCModule {
    #[must_use]
    pub fn new(handler: Arc<Handlers>) -> Self {
        Self { handler }
    }

    fn block_number_to_u256(&self, block_number: Option<BlockNumber>) -> U256 {
        match block_number.unwrap_or_default() {
            BlockNumber::Hash {
                hash,
                ..
            } => {
                self.handler
                    .storage
                    .get_block_by_hash(&hash)
                    .map(|block| block.header.number)
                    .unwrap_or_default()
            }
            BlockNumber::Num(n) => {
                self.handler
                    .storage
                    .get_block_by_number(&U256::from(n))
                    .map(|block| block.header.number)
                    .unwrap_or_default()
            },
            _ => {
                self.handler
                    .storage
                    .get_latest_block()
                    .map(|block| block.header.number)
                    .unwrap_or_default()
            }
            // BlockNumber::Earliest => todo!(),
            // BlockNumber::Pending => todo!(),
            // BlockNumber::Safe => todo!(),
            // BlockNumber::Finalized => todo!(),
        }
    }
}

impl MetachainRPCServer for MetachainRPCModule {
    fn call(&self, input: CallRequest) -> RpcResult<String> {
        let CallRequest {
            from,
            to,
            gas,
            value,
            data,
            ..
        } = input;
        let TxResponse { data, .. } = self
            .handler
            .evm
            .call(
                from,
                to,
                value.unwrap_or_default(),
                &data.unwrap_or_default(),
                gas.unwrap_or_default().as_u64(),
                vec![],
            )
            .map_err(|e| Error::Custom(format!("Error getting address balance : {e:?}")))?;

        Ok(hex::encode(data))
    }

    fn accounts(&self) -> RpcResult<Vec<String>> {
        let accounts = ain_cpp_imports::get_accounts().unwrap();
        Ok(accounts)
    }

    // State RPC

    fn get_balance(&self, address: H160, block_number: Option<BlockNumber>) -> RpcResult<U256> {
        let block_number = self.block_number_to_u256(block_number);
        debug!(
            "Getting balance for address: {:?} at block : {} ",
            address, block_number
        );
        self.handler
            .evm
            .get_balance(address, block_number)
            .map_err(|e| Error::Custom(format!("Error getting address balance : {e:?}")))
    }

    fn get_code(&self, address: H160, block_number: Option<BlockNumber>) -> RpcResult<String> {
        let block_number = self.block_number_to_u256(block_number);

        debug!(
            "Getting code for address: {:?} at block : {}",
            address, block_number
        );

        let code = self
            .handler
            .evm
            .get_code(address, block_number)
            .map_err(|e| Error::Custom(format!("Error getting address code : {e:?}")))?;

        match code {
            Some(code) => Ok(format!("0x{}", hex::encode(code))),
            None => Ok(String::from("0x")),
        }
    }

    fn get_storage_at(
        &self,
        address: H160,
        position: U256,
        block_number: Option<BlockNumber>,
    ) -> RpcResult<H256> {
        let block_number = self.block_number_to_u256(block_number);
        debug!(
            "Getting storage for address: {:?}, at position {:?}, for block {}",
            address, position, block_number
        );

        self.handler
            .evm
            .get_storage_at(address, position, block_number)
            .map_err(|e| Error::Custom(format!("Error getting address storage at : {e:?}")))?
            .map_or(Ok(H256::default()), |storage| {
                Ok(H256::from_slice(&storage))
            })
    }
    // ------

    fn get_block_by_hash(&self, hash: H256) -> RpcResult<Option<RpcBlock>> {
        self.handler
            .storage
            .get_block_by_hash(&hash)
            .map_or(Ok(None), |block| Ok(Some(block.into())))
    }

    fn chain_id(&self) -> RpcResult<String> {
        let chain_id = ain_cpp_imports::get_chain_id()
            .map_err(|e| Error::Custom(format!("ain_cpp_imports::get_chain_id error : {e:?}")))?;

        Ok(format!("{chain_id:#x}"))
    }

    fn hash_rate(&self) -> RpcResult<String> {
        Ok(String::from("0x0"))
    }

    fn net_version(&self) -> RpcResult<String> {
        let chain_id = ain_cpp_imports::get_chain_id()
            .map_err(|e| Error::Custom(format!("ain_cpp_imports::get_chain_id error : {e:?}")))?;

        Ok(format!("{chain_id}"))
    }

    fn block_number(&self) -> RpcResult<U256> {
        let count = self
            .handler
            .storage
            .get_latest_block()
            .map(|block| block.header.number)
            .unwrap_or_default();

        debug!("Current block number: {:?}", count);
        Ok(count)
    }

    fn get_block_by_number(
        &self,
        block_number: BlockNumber,
        _full_transaction: bool,
    ) -> RpcResult<Option<RpcBlock>> {
        debug!("Getting block by number : {:#?}", block_number);
        let block_number = self.block_number_to_u256(Some(block_number));
        self.handler
            .storage
            .get_block_by_number(&block_number)
            .map_or(Ok(None), |block| Ok(Some(block.into())))
    }

    fn mining(&self) -> RpcResult<bool> {
        ain_cpp_imports::is_mining().map_err(|e| Error::Custom(e.to_string()))
    }

    fn get_transaction_by_hash(&self, hash: H256) -> RpcResult<Option<EthTransactionInfo>> {
        self.handler
            .storage
            .get_transaction_by_hash(&hash)
            .map_or(Ok(None), |tx| {
                let transaction_info = tx
                    .try_into()
                    .map_err(|e: TransactionError| Error::Custom(e.to_string()))?;
                Ok(Some(transaction_info))
            })
    }

    fn get_pending_transaction(&self) -> RpcResult<Vec<EthPendingTransactionInfo>> {
        ain_cpp_imports::get_pool_transactions()
            .map(|txs| {
                txs.into_iter()
                    .flat_map(|tx| EthPendingTransactionInfo::try_from(tx.as_str()))
                    .collect()
            })
            .map_err(|e| Error::Custom(e.to_string()))
    }

    fn get_transaction_by_block_hash_and_index(
        &self,
        hash: H256,
        index: usize,
    ) -> RpcResult<Option<EthTransactionInfo>> {
        self.handler
            .storage
            .get_transaction_by_block_hash_and_index(&hash, index)
            .map_or(Ok(None), |tx| {
                let transaction_info = tx
                    .try_into()
                    .map_err(|e: TransactionError| Error::Custom(e.to_string()))?;
                Ok(Some(transaction_info))
            })
    }

    fn get_transaction_by_block_number_and_index(
        &self,
        number: U256,
        index: usize,
    ) -> RpcResult<Option<EthTransactionInfo>> {
        self.handler
            .storage
            .get_transaction_by_block_number_and_index(&number, index)
            .map_or(Ok(None), |tx| {
                let transaction_info = tx
                    .try_into()
                    .map_err(|e: TransactionError| Error::Custom(e.to_string()))?;
                Ok(Some(transaction_info))
            })
    }

    fn get_block_transaction_count_by_hash(&self, hash: H256) -> RpcResult<usize> {
        self.handler
            .storage
            .get_block_by_hash(&hash)
            .map_or(Ok(0), |b| Ok(b.transactions.len()))
    }

    fn get_block_transaction_count_by_number(&self, block_number: BlockNumber) -> RpcResult<usize> {
        let block_number = self.block_number_to_u256(Some(block_number));
        self.handler
            .storage
            .get_block_by_number(&block_number)
            .map_or(Ok(0), |b| Ok(b.transactions.len()))
    }

    fn send_raw_transaction(&self, tx: &str) -> RpcResult<String> {
        debug!("Sending raw transaction: {:?}", tx);
        let raw_tx = tx.strip_prefix("0x").unwrap_or(tx);
        let hex =
            hex::decode(raw_tx).map_err(|e| Error::Custom(format!("Eror decoding TX {e:?}")))?;
        let stat = ain_cpp_imports::publish_eth_transaction(hex)
            .map_err(|e| Error::Custom(format!("Error publishing TX {e:?}")))?;

        let signed_tx =
            SignedTx::try_from(raw_tx).map_err(|e| Error::Custom(format!("TX error {e:?}")))?;

        debug!(
            "Transaction status: {:?}, hash: {}",
            stat,
            signed_tx.transaction.hash()
        );
        Ok(format!("{:#x}", signed_tx.transaction.hash()))
    }

    fn get_transaction_count(
        &self,
        address: H160,
        block_number: Option<BlockNumber>,
    ) -> RpcResult<String> {
        debug!("Getting transaction count for address: {:?}", address);
        let block_number = self.block_number_to_u256(block_number);
        let nonce = self
            .handler
            .evm
            .get_nonce(address, block_number)
            .map_err(|e| {
                Error::Custom(format!("Error getting address transaction count : {e:?}"))
            })?;

        debug!("Count: {:#?}", nonce);
        Ok(format!("{nonce:#x}"))
    }

    fn estimate_gas(&self, input: CallRequest) -> RpcResult<String> {
        let CallRequest {
            from,
            to,
            gas,
            value,
            data,
            ..
        } = input;

        let TxResponse { data, used_gas, .. } = self
            .handler
            .evm
            .call(
                from,
                to,
                value.unwrap_or_default(),
                &data.unwrap_or_default(),
                gas.unwrap_or_default().as_u64(),
                vec![],
            )
            .map_err(|e| Error::Custom(format!("Error calling EVM : {e:?}")))?;

        let native_size = ain_cpp_imports::get_native_tx_size(data).unwrap_or(0);
        debug!("estimateGas: {:#?} + {:#?}", native_size, used_gas);
        Ok(format!(
            "{:#x}",
            native_size + std::cmp::max(21000, used_gas)
        ))
    }

    fn gas_price(&self) -> RpcResult<String> {
        let gas_price = ain_cpp_imports::get_min_relay_tx_fee().unwrap_or(10);
        debug!("gasPrice: {:#?}", gas_price);
        Ok(format!("{gas_price:#x}"))
    }

    fn get_receipt(&self, hash: H256) -> RpcResult<Option<ReceiptResult>> {
        self.handler
            .storage
            .get_receipt(&hash)
            .map_or(Ok(None), |receipt| Ok(Some(ReceiptResult::from(receipt))))
    }

    fn get_getwork(&self) -> RpcResult<Vec<String>> {
        Ok(vec![
            "0x0000000000000000000000000000000000000000000000000000000000000000".to_string(),
            "0x0000000000000000000000000000000000000000000000000000000000000000".to_string(),
            "0x0000000000000000000000000000000000000000000000000000000000000000".to_string(),
        ])
    }

    fn eth_submitwork(&self, _nonce: String, _hash: String, _digest: String) -> RpcResult<bool> {
        Ok(false)
    }

    fn eth_submithashrate(&self, _hashrate: String, _id: String) -> RpcResult<bool> {
        Ok(false)
    }
}
