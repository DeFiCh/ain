use crate::block::{BlockNumber, RpcBlock};
use crate::call_request::CallRequest;
use crate::codegen::types::{EthTransactionInfo, EthPendingTransactionInfo};

use ain_cpp_imports::get_pool_transactions;
use ain_evm::evm::EVMState;
use ain_evm::handler::Handlers;
use ain_evm::transaction::{SignedTx, TransactionError};
use jsonrpsee::core::{Error, RpcResult};
use ethereum::{Block, PartialHeader, TransactionV2};
use jsonrpsee::proc_macros::rpc;
use log::debug;
use primitive_types::{H160, H256, U256};
use core::num::flt2dec::decode;
use std::convert::Into;
use std::sync::Arc;

#[rpc(server, client)]
pub trait MetachainRPC {
    #[method(name = "eth_call")]
    fn call(&self, input: CallRequest) -> RpcResult<String>;

    #[method(name = "eth_accounts")]
    fn accounts(&self) -> RpcResult<Vec<String>>;

    #[method(name = "eth_getBalance")]
    fn get_balance(&self, address: H160) -> RpcResult<U256>;

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
    fn get_pending_transaction(&self) -> Result<Vec<EthPendingTransactionInfo>>;

    #[method(name = "eth_getCode")]
    fn get_code(&self, address: H160) -> RpcResult<String>;

    #[method(name = "eth_getStorageAt")]
    fn get_storage_at(&self, address: H160, position: H256) -> RpcResult<String>;

    #[method(name = "eth_sendRawTransaction")]
    fn send_raw_transaction(&self, tx: &str) -> RpcResult<String>;

    #[method(name = "eth_getTransactionCount")]
    fn get_transaction_count(&self, address: H160) -> RpcResult<String>;

    #[method(name = "eth_estimateGas")]
    fn estimate_gas(&self) -> RpcResult<String>;

    #[method(name = "mc_getState")]
    fn get_state(&self) -> RpcResult<EVMState>;

    #[method(name = "eth_gasPrice")]
    fn gas_price(&self) -> RpcResult<String>;
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
    fn call(&self, input: CallRequest) -> RpcResult<String> {
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

    fn accounts(&self) -> RpcResult<Vec<String>> {
        let accounts = ain_cpp_imports::get_accounts().unwrap();
        Ok(accounts)
    }

    fn get_balance(&self, address: H160) -> RpcResult<U256> {
        debug!("Getting balance for address: {:?}", address);
        Ok(self.handler.evm.get_balance(address))
    }

    fn get_block_by_hash(&self, hash: H256) -> RpcResult<Option<RpcBlock>> {
        self.handler
            .storage
            .get_block_by_hash(&hash)
            .map_or(Ok(None), |block| Ok(Some(block.into())))
    }

    fn chain_id(&self) -> RpcResult<String> {
        let chain_id = ain_cpp_imports::get_chain_id()
            .map_err(|e| Error::Custom(format!("ain_cpp_imports::get_chain_id error : {:?}", e)))?;

        Ok(format!("{:#x}", chain_id))
    }

    fn hash_rate(&self) -> RpcResult<String> {
        Ok(String::from("0x0"))
    }

    fn net_version(&self) -> RpcResult<String> {
        let chain_id = ain_cpp_imports::get_chain_id()
            .map_err(|e| Error::Custom(format!("ain_cpp_imports::get_chain_id error : {:?}", e)))?;

        Ok(format!("{}", chain_id))
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
        match block_number {
            BlockNumber::Num(number) => {
                let number = U256::from(number);
                self.handler
                    .storage
                    .get_block_by_number(&number)
                    .map_or(Ok(None), |block| Ok(Some(block.into())))
            }
            BlockNumber::Latest => self
                .handler
                .storage
                .get_latest_block()
                .map_or(Ok(None), |block| Ok(Some(block.into()))),
            _ => Ok(None),
            // BlockNumber::Hash { hash, require_canonical } => todo!(),
            // BlockNumber::Earliest => todo!(),
            // BlockNumber::Pending => todo!(),
            // BlockNumber::Safe => todo!(),
            // BlockNumber::Finalized => todo!(),
        }
    }

    fn mining(&self) -> RpcResult<bool> {
        ain_cpp_imports::is_mining().map_err(|e| Error::Custom(e.to_string()))
    }

    fn get_transaction_by_hash(&self, hash: H256) -> RpcResult<Option<EthTransactionInfo>> {
        self.handler
            .storage
            .get_transaction_by_hash(hash)
            .map_or(Ok(None), |tx| {
                let transaction_info = tx
                    .try_into()
                    .map_err(|e: TransactionError| Error::Custom(e.to_string()))?;
                Ok(Some(transaction_info))
            })
    }

    fn get_pending_transaction(&self) -> Result<Vec<EthPendingTransactionInfo>> {
        let mut transactions = Vec::new();

        let pool_transactions = get_pool_transactions();

        if let Ok(pool_transactions) = pool_transactions {
            for raw_transaction in pool_transaction.iter() {
                let decode_result = ethereum::EnvelopedDecodable::decode(&raw_transaction);

                let tx: TransactionV2 = decode_result.unwrap_or(
                    continue
                );

                let signed_tx: SignedTx = tx.try_into().unwrap_or(
                    continue
                );

                let mut pending_transaction: EthPendingTransactionInfo = {};
                pending_transaction.hash = signed_tx.transaction.hash().to_string();
                pending_transaction.nonce = signed_tx.nonce().to_string();
                pending_transaction.block_hash = String::from("0000000000000000000000000000000000000000000000000000000000000000");
                pending_transaction.block_number = String::from("null");
                pending_transaction.transaction_index = String::from("0x0");
                pending_transaction.from = String::from("0x").push_str(&hex::encode(signed_tx.sender.as_fixed_bytes()));
                let to = signed_tx.to();
                if let Some(to) = to {
                    pending_transaction.to = String::from("0x").push_str(&hex::encode(to.as_fixed_bytes()));
                } else {
                    pending_transaction.to = String::from("0x0");
                }
                pending_transaction.value = signed_tx.value().to_string();
                pending_transaction.gas = signed_tx.gas_limit().to_string();
                pending_transaction.gas_price = signed_tx.gas_price().to_string();
                pending_transaction.input = hex::encode(signed_tx.data());
                /* TODO Add calls to get v, r and s for different TX types
                pending_transaction.v = ;
                pending_transaction.r = ;
                pending_transaction.s = ;
                 */

                transactions.push(pending_transaction);
            }
        }

        Ok(transactions)
    }

    fn get_transaction_by_block_hash_and_index(
        &self,
        hash: H256,
        index: usize,
    ) -> RpcResult<Option<EthTransactionInfo>> {
        self.handler
            .storage
            .get_transaction_by_block_hash_and_index(hash, index)
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
            .get_transaction_by_block_number_and_index(number, index)
            .map_or(Ok(None), |tx| {
                let transaction_info = tx
                    .try_into()
                    .map_err(|e: TransactionError| Error::Custom(e.to_string()))?;
                Ok(Some(transaction_info))
            })
    }

    fn get_block_transaction_count_by_hash(&self, hash: H256) -> RpcResult<usize> {
        self.handler
            .block
            .get_block_by_hash(hash)
            .map_or(Ok(0), |b| Ok(b.transactions.len()))
    }

    fn get_block_transaction_count_by_number(&self, number: BlockNumber) -> RpcResult<usize> {
        match number {
            BlockNumber::Pending => Ok(0), // TODO get from mempool ?
            BlockNumber::Num(number) if number > 0 => self
                .handler
                .block
                .get_block_by_number(number as usize)
                .map_or(Ok(0), |b| Ok(b.transactions.len())),
            BlockNumber::Num(_) => Err(Error::Custom(String::from("Block number should be >= 0."))),
            BlockNumber::Latest => self
                .handler
                .storage
                .get_latest_block()
                .map_or(Ok(0), |b| Ok(b.transactions.len())),
            BlockNumber::Hash {
                ..
                // hash,
                // require_canonical,
            } => todo!(),
            BlockNumber::Earliest => todo!(),
            BlockNumber::Safe => todo!(),
            BlockNumber::Finalized => todo!(),
        }
    }

    fn get_code(&self, address: H160) -> RpcResult<String> {
        debug!("Getting code for address: {:?}", address);
        let code = self.handler.evm.get_code(address);

        if code.is_empty() {
            return Ok(String::from("0x"));
        }

        Ok(format!("{:#x?}", code))
    }

    fn get_storage_at(&self, address: H160, position: H256) -> RpcResult<String> {
        debug!(
            "Getting storage for address: {:?}, at position {:?}",
            address, position
        );
        let storage = self.handler.evm.get_storage(address);
        let &value = storage.get(&position).unwrap_or(&H256::zero());

        Ok(format!("{:#x}", value))
    }

    fn send_raw_transaction(&self, tx: &str) -> RpcResult<String> {
        debug!("Sending raw transaction: {:?}", tx);
        let raw_tx = tx.strip_prefix("0x").unwrap_or(tx);
        let hex =
            hex::decode(raw_tx).map_err(|e| Error::Custom(format!("Eror decoding TX {:?}", e)))?;
        let stat = ain_cpp_imports::publish_eth_transaction(hex)
            .map_err(|e| Error::Custom(format!("Error publishing TX {:?}", e)))?;

        let signed_tx =
            SignedTx::try_from(raw_tx).map_err(|e| Error::Custom(format!("TX error {:?}", e)))?;

        debug!(
            "Transaction status: {:?}, hash: {}",
            stat,
            signed_tx.transaction.hash()
        );
        Ok(format!("{:#x}", signed_tx.transaction.hash()))
    }

    fn get_transaction_count(&self, address: H160) -> RpcResult<String> {
        debug!("Getting transaction count for address: {:?}", address);
        let nonce = self.handler.evm.get_nonce(address);

        debug!("Count: {:#?}", nonce,);
        Ok(format!("{:#x}", nonce))
    }

    fn estimate_gas(&self) -> RpcResult<String> {
        Ok(format!("{:#x}", 21000))
    }

    fn get_state(&self) -> RpcResult<EVMState> {
        Ok(self.handler.evm.state.read().unwrap().clone())
    }

    fn gas_price(&self) -> RpcResult<String> {
        Ok(format!("{:#x}", 0))
    }
}
