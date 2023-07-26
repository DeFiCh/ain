use crate::block::{BlockNumber, RpcBlock, RpcFeeHistory};
use crate::call_request::CallRequest;
use crate::codegen::types::EthTransactionInfo;
use ain_evm::bytes::Bytes;

use crate::receipt::ReceiptResult;
use crate::transaction_request::{TransactionMessage, TransactionRequest};
use crate::utils::{format_h256, format_u256};
use ain_cpp_imports::get_eth_priv_key;
use ain_evm::core::{EthCallArgs, MAX_GAS_PER_BLOCK};
use ain_evm::evm::EVMServices;
use ain_evm::executor::TxResponse;

use crate::filters::{GetFilterChangesResult, NewFilterRequest};
use crate::sync::{SyncInfo, SyncState};
use crate::transaction_log::{GetLogsRequest, LogResult};
use ain_evm::filters::Filter;
use ain_evm::storage::traits::{BlockStorage, ReceiptStorage, TransactionStorage};
use ain_evm::transaction::{SignedTx, TransactionError};
use ethereum::{EnvelopedEncodable, TransactionV2};
use jsonrpsee::core::{Error, RpcResult};
use jsonrpsee::proc_macros::rpc;
use libsecp256k1::SecretKey;
use log::{debug, trace};
use primitive_types::{H160, H256, U256};
use std::convert::Into;
use std::str::FromStr;
use std::sync::Arc;

#[rpc(server, client, namespace = "eth")]
pub trait MetachainRPC {
    // ----------------------------------------
    // Client
    // ----------------------------------------

    /// Makes a call to the Ethereum node without creating a transaction on the blockchain.
    /// Returns the output data as a hexadecimal string.
    #[method(name = "call")]
    fn call(&self, input: CallRequest, block_number: Option<BlockNumber>) -> RpcResult<Bytes>;

    /// Retrieves the list of accounts managed by the node.
    /// Returns a vector of Ethereum addresses as hexadecimal strings.
    #[method(name = "accounts")]
    fn accounts(&self) -> RpcResult<Vec<String>>;

    /// Returns the current chain ID as a hexadecimal string.
    #[method(name = "chainId")]
    fn chain_id(&self) -> RpcResult<String>;

    #[method(name = "syncing")]
    fn syncing(&self) -> RpcResult<SyncState>;

    // ----------------------------------------
    // Block
    // ----------------------------------------

    /// Returns the current block number as U256.
    #[method(name = "blockNumber")]
    fn block_number(&self) -> RpcResult<U256>;

    /// Retrieves a specific block, identified by its block number.
    /// Returns full transaction info or transaction hash depending on full_transactions param
    #[method(name = "getBlockByNumber")]
    fn get_block_by_number(
        &self,
        block_number: BlockNumber,
        full_transactions: Option<bool>,
    ) -> RpcResult<Option<RpcBlock>>;

    /// Retrieves a specific block, identified by its hash.
    #[method(name = "getBlockByHash")]
    fn get_block_by_hash(
        &self,
        hash: H256,
        full_transactions: Option<bool>,
    ) -> RpcResult<Option<RpcBlock>>;

    /// Retrieves the transaction count for a specific block, identified by its hash.
    #[method(name = "getBlockTransactionCountByHash")]
    fn get_block_transaction_count_by_hash(&self, hash: H256) -> RpcResult<usize>;

    /// Retrieves the transaction count for a specific block, identified by its block number.
    #[method(name = "getBlockTransactionCountByNumber")]
    fn get_block_transaction_count_by_number(&self, number: BlockNumber) -> RpcResult<usize>;

    // ----------------------------------------
    // Mining
    // ----------------------------------------

    /// Checks if the node is currently mining.
    #[method(name = "mining")]
    fn mining(&self) -> RpcResult<bool>;

    /// Returns the hash of the current block, the seedHash, and the boundary condition to be met ("target").
    #[method(name = "getWork")]
    fn get_getwork(&self) -> RpcResult<Vec<String>>;

    /// Submits a proof of work solution to the node.
    /// Always returns false
    #[method(name = "submitWork")]
    fn eth_submitwork(&self, nonce: String, hash: String, digest: String) -> RpcResult<bool>;

    /// Retrieves the current hash rate of the node.
    /// Always returns 0x0
    #[method(name = "hashrate")]
    fn hash_rate(&self) -> RpcResult<String>;

    /// Submit mining hashrate.
    /// Always returns false
    #[method(name = "submitHashrate")]
    fn eth_submithashrate(&self, hashrate: String, id: String) -> RpcResult<bool>;

    // ----------------------------------------
    // Transaction
    // ----------------------------------------

    /// Retrieves a specific transaction, identified by its hash.
    #[method(name = "getTransactionByHash")]
    fn get_transaction_by_hash(&self, hash: H256) -> RpcResult<Option<EthTransactionInfo>>;

    /// Retrieves a specific transaction, identified by the block hash and transaction index.
    #[method(name = "getTransactionByBlockHashAndIndex")]
    fn get_transaction_by_block_hash_and_index(
        &self,
        hash: H256,
        index: usize,
    ) -> RpcResult<Option<EthTransactionInfo>>;

    /// Retrieves a specific transaction, identified by the block number and transaction index.
    #[method(name = "getTransactionByBlockNumberAndIndex")]
    fn get_transaction_by_block_number_and_index(
        &self,
        block_number: U256,
        index: usize,
    ) -> RpcResult<Option<EthTransactionInfo>>;

    /// Retrieves the list of pending transactions.
    #[method(name = "pendingTransactions")]
    fn get_pending_transaction(&self) -> RpcResult<Vec<EthTransactionInfo>>;

    /// Retrieves the receipt of a specific transaction, identified by its hash.
    #[method(name = "getTransactionReceipt")]
    fn get_receipt(&self, hash: H256) -> RpcResult<Option<ReceiptResult>>;

    // ----------------------------------------
    // Account state
    // ----------------------------------------

    /// Retrieves the balance of a specific Ethereum address at a given block number.
    /// Returns the balance as U256.
    #[method(name = "getBalance")]
    fn get_balance(&self, address: H160, block_number: Option<BlockNumber>) -> RpcResult<U256>;

    /// Retrieves the bytecode of a contract at a specific address.
    #[method(name = "getCode")]
    fn get_code(&self, address: H160, block_number: Option<BlockNumber>) -> RpcResult<String>;

    /// Retrieves the storage value at a specific position in a contract.
    #[method(name = "getStorageAt")]
    fn get_storage_at(
        &self,
        address: H160,
        position: U256,
        block_number: Option<BlockNumber>,
    ) -> RpcResult<H256>;

    /// Retrieves the number of transactions sent from a specific address.
    #[method(name = "getTransactionCount")]
    fn get_transaction_count(
        &self,
        address: H160,
        block_number: Option<BlockNumber>,
    ) -> RpcResult<U256>;

    // ----------------------------------------
    // Sign
    // ----------------------------------------

    /// Signs a transaction.
    /// Retuns a raw transaction as a hexademical string.
    #[method(name = "signTransaction")]
    fn sign_transaction(&self, req: TransactionRequest) -> RpcResult<String>;

    // ----------------------------------------
    // Send
    // ----------------------------------------

    /// Sends a transaction.
    /// Returns the transaction hash as a hexadecimal string.
    #[method(name = "sendTransaction")]
    fn send_transaction(&self, req: TransactionRequest) -> RpcResult<String>;

    /// Sends a signed transaction.
    /// Returns the transaction hash as a hexadecimal string.
    #[method(name = "sendRawTransaction")]
    fn send_raw_transaction(&self, tx: &str) -> RpcResult<String>;

    // ----------------------------------------
    // Gas
    // ----------------------------------------

    /// Estimate gas needed for execution of given contract.
    #[method(name = "estimateGas")]
    fn estimate_gas(
        &self,
        input: CallRequest,
        block_number: Option<BlockNumber>,
    ) -> RpcResult<U256>;

    /// Returns current gas_price.
    #[method(name = "gasPrice")]
    fn gas_price(&self) -> RpcResult<U256>;

    #[method(name = "feeHistory")]
    fn fee_history(
        &self,
        block_count: U256,
        first_block: U256,
        priority_fee_percentile: Vec<usize>,
    ) -> RpcResult<RpcFeeHistory>;

    #[method(name = "maxPriorityFeePerGas")]
    fn max_priority_fee_per_gas(&self) -> RpcResult<U256>;

    // ----------------------------------------
    // Uncle blocks
    // All methods return null or default values as we do not have uncle blocks
    // ----------------------------------------
    #[method(name = "getUncleCountByBlockNumber")]
    fn get_uncle_count_by_block_number(&self) -> RpcResult<U256>;
    //
    #[method(name = "getUncleCountByBlockHash")]
    fn get_uncle_count_by_block_hash(&self) -> RpcResult<U256>;

    #[method(name = "getUncleByBlockNumberAndIndex")]
    fn get_uncle_by_block_number(&self) -> RpcResult<Option<bool>>;

    #[method(name = "getUncleByBlockHashAndIndex")]
    fn get_uncle_by_block_hash(&self) -> RpcResult<Option<bool>>;

    // ----------------------------------------
    // Logs and filters
    // ----------------------------------------

    #[method(name = "getLogs")]
    fn get_logs(&self, input: GetLogsRequest) -> RpcResult<Vec<LogResult>>;

    #[method(name = "newFilter")]
    fn new_filter(&self, input: NewFilterRequest) -> RpcResult<U256>;

    #[method(name = "newBlockFilter")]
    fn new_block_filter(&self) -> RpcResult<U256>;

    #[method(name = "getFilterChanges")]
    fn get_filter_changes(&self, filter_id: U256) -> RpcResult<GetFilterChangesResult>;

    #[method(name = "uninstallFilter")]
    fn uninstall_filter(&self, filter_id: U256) -> RpcResult<bool>;

    #[method(name = "getFilterLogs")]
    fn get_filter_logs(&self, filter_id: U256) -> RpcResult<Vec<LogResult>>;

    #[method(name = "newPendingTransactionFilter")]
    fn new_pending_transaction_filter(&self) -> RpcResult<U256>;
}

pub struct MetachainRPCModule {
    handler: Arc<EVMServices>,
}

impl MetachainRPCModule {
    #[must_use]
    pub fn new(handler: Arc<EVMServices>) -> Self {
        Self { handler }
    }

    fn block_number_to_u256(&self, block_number: Option<BlockNumber>) -> RpcResult<U256> {
        match block_number.unwrap_or_default() {
            BlockNumber::Hash {
                hash,
                ..
            } => {
                self.handler
                    .storage
                    .get_block_by_hash(&hash)
                    .map(|block| block.header.number)
            }
            BlockNumber::Num(n) => {
                self.handler
                    .storage
                    .get_block_by_number(&U256::from(n))
                    .map(|block| block.header.number)
            }
            BlockNumber::Earliest => {
                self.handler
                    .storage
                    .get_block_by_number(&U256::zero())
                    .map(|block| block.header.number)
            }
            _ => {
                self.handler
                    .storage
                    .get_latest_block()
                    .map(|block| block.header.number)
            }
            // BlockNumber::Pending => todo!(),
            // BlockNumber::Safe => todo!(),
            // BlockNumber::Finalized => todo!(),
        }
        .ok_or(Error::Custom(String::from("header not found")))
    }
}

impl MetachainRPCServer for MetachainRPCModule {
    fn call(&self, input: CallRequest, block_number: Option<BlockNumber>) -> RpcResult<Bytes> {
        debug!(target:"rpc",  "[RPC] Call input {:#?}", input);
        let CallRequest {
            from,
            to,
            gas,
            value,
            data,
            input,
            access_list,
            ..
        } = input;
        let TxResponse { data, .. } = self
            .handler
            .core
            .call(EthCallArgs {
                caller: from,
                to,
                value: value.unwrap_or_default(),
                // https://github.com/ethereum/go-ethereum/blob/281e8cd5abaac86ed3f37f98250ff147b3c9fe62/internal/ethapi/transaction_args.go#L67
                // We accept "data" and "input" for backwards-compatibility reasons.
                //  "input" is the newer name and should be preferred by clients.
                // 	Issue detail: https://github.com/ethereum/go-ethereum/issues/15628
                data: &input
                    .map(|d| d.0)
                    .unwrap_or(data.map(|d| d.0).unwrap_or_default()),
                gas_limit: gas.unwrap_or(MAX_GAS_PER_BLOCK).as_u64(),
                access_list: access_list.unwrap_or_default(),
                block_number: self.block_number_to_u256(block_number)?,
            })
            .map_err(|e| Error::Custom(format!("Error calling EVM : {e:?}")))?;
        Ok(Bytes(data))
    }

    fn accounts(&self) -> RpcResult<Vec<String>> {
        let accounts = ain_cpp_imports::get_accounts().unwrap();
        Ok(accounts)
    }

    // State RPC

    fn get_balance(&self, address: H160, block_number: Option<BlockNumber>) -> RpcResult<U256> {
        let block_number = self.block_number_to_u256(block_number)?;
        debug!(target:"rpc",
            "Getting balance for address: {:?} at block : {} ",
            address, block_number
        );
        let balance = self
            .handler
            .core
            .get_balance(address, block_number)
            .unwrap_or(U256::zero());

        debug!(target:"rpc", "Address: {:?} balance : {} ", address, balance);
        Ok(balance)
    }

    fn get_code(&self, address: H160, block_number: Option<BlockNumber>) -> RpcResult<String> {
        let block_number = self.block_number_to_u256(block_number)?;

        debug!(target:"rpc",
            "Getting code for address: {:?} at block : {}",
            address, block_number
        );

        let code = self
            .handler
            .core
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
        let block_number = self.block_number_to_u256(block_number)?;
        debug!(target:"rpc",
            "Getting storage for address: {:?}, at position {:?}, for block {}",
            address, position, block_number
        );

        self.handler
            .core
            .get_storage_at(address, position, block_number)
            .map_err(|e| Error::Custom(format!("get_storage_at error : {e:?}")))?
            .map_or(Ok(H256::default()), |storage| {
                Ok(H256::from_slice(&storage))
            })
    }

    fn get_block_by_hash(
        &self,
        hash: H256,
        full_transactions: Option<bool>,
    ) -> RpcResult<Option<RpcBlock>> {
        debug!("Getting block by hash {:#x}", hash);
        self.handler
            .storage
            .get_block_by_hash(&hash)
            .map_or(Ok(None), |block| {
                Ok(Some(RpcBlock::from_block_with_tx(
                    block,
                    full_transactions.unwrap_or_default(),
                )))
            })
    }

    fn chain_id(&self) -> RpcResult<String> {
        let chain_id = ain_cpp_imports::get_chain_id()
            .map_err(|e| Error::Custom(format!("ain_cpp_imports::get_chain_id error : {e:?}")))?;

        Ok(format!("{chain_id:#x}"))
    }

    fn hash_rate(&self) -> RpcResult<String> {
        Ok(String::from("0x0"))
    }

    fn block_number(&self) -> RpcResult<U256> {
        let count = self
            .handler
            .storage
            .get_latest_block()
            .map(|block| block.header.number)
            .unwrap_or_default();

        trace!(target:"rpc",  "Current block number: {:?}", count);
        Ok(count)
    }

    fn get_block_by_number(
        &self,
        block_number: BlockNumber,
        full_transactions: Option<bool>,
    ) -> RpcResult<Option<RpcBlock>> {
        let block_number = self.block_number_to_u256(Some(block_number))?;
        debug!(target:"rpc", "Getting block by number : {}", block_number);
        self.handler
            .storage
            .get_block_by_number(&block_number)
            .map_or(Ok(None), |block| {
                Ok(Some(RpcBlock::from_block_with_tx(
                    block,
                    full_transactions.unwrap_or_default(),
                )))
            })
    }

    fn mining(&self) -> RpcResult<bool> {
        ain_cpp_imports::is_mining().map_err(|e| Error::Custom(e.to_string()))
    }

    fn get_transaction_by_hash(&self, hash: H256) -> RpcResult<Option<EthTransactionInfo>> {
        self.handler
            .storage
            .get_transaction_by_hash(&hash)
            .map_or(Ok(None), |tx| {
                let mut transaction_info: EthTransactionInfo = tx
                    .try_into()
                    .map_err(|e: TransactionError| Error::Custom(e.to_string()))?;

                // TODO: Improve efficiency by indexing the block_hash, block_number, and transaction_index fields.
                // Temporary workaround: Makes an additional call to get_receipt where these fields are available.
                if let Some(receipt) = self.handler.storage.get_receipt(&hash) {
                    transaction_info.block_hash = Some(format_h256(receipt.block_hash));
                    transaction_info.block_number = Some(format_u256(receipt.block_number));
                    transaction_info.transaction_index =
                        Some(format_u256(U256::from(receipt.tx_index)));
                }

                Ok(Some(transaction_info))
            })
    }

    fn get_pending_transaction(&self) -> RpcResult<Vec<EthTransactionInfo>> {
        ain_cpp_imports::get_pool_transactions()
            .map(|txs| {
                txs.into_iter()
                    .flat_map(|tx| EthTransactionInfo::try_from(tx.as_str()))
                    .map(EthTransactionInfo::into_pending_transaction_info)
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
        let block_number = self.block_number_to_u256(Some(block_number))?;
        self.handler
            .storage
            .get_block_by_number(&block_number)
            .map_or(Ok(0), |b| Ok(b.transactions.len()))
    }

    fn sign_transaction(&self, request: TransactionRequest) -> RpcResult<String> {
        debug!(target:"rpc", "[sign_transaction] signing transaction: {:?}", request);

        let from = match request.from {
            Some(from) => from,
            None => {
                let accounts = self.accounts()?;

                match accounts.get(0) {
                    Some(account) => H160::from_str(account.as_str()).unwrap(),
                    None => return Err(Error::Custom(String::from("from is not available"))),
                }
            }
        };
        debug!(target:"rpc", "[send_transaction] from: {:?}", from);

        let chain_id = ain_cpp_imports::get_chain_id()
            .map_err(|e| Error::Custom(format!("ain_cpp_imports::get_chain_id error : {e:?}")))?;

        let block_number = self.block_number()?;

        let nonce = match request.nonce {
            Some(nonce) => nonce,
            None => self
                .handler
                .core
                .get_nonce(from, block_number)
                .map_err(|e| {
                    Error::Custom(format!("Error getting address transaction count : {e:?}"))
                })?,
        };

        let gas_price = request.gas_price;
        let gas_limit = match request.gas {
            Some(gas_limit) => gas_limit,
            // TODO(): get the gas_limit from block.header
            // set 21000 (min gas_limit req) by default first
            None => U256::from(21000),
        };
        let max_fee_per_gas = request.max_fee_per_gas;
        let message: Option<TransactionMessage> = request.into();
        let message = match message {
            Some(TransactionMessage::Legacy(mut m)) => {
                m.nonce = nonce;
                m.chain_id = Some(chain_id);
                m.gas_limit = gas_limit;
                if gas_price.is_none() {
                    m.gas_price = self.gas_price().unwrap();
                }
                TransactionMessage::Legacy(m)
            }
            Some(TransactionMessage::EIP2930(mut m)) => {
                m.nonce = nonce;
                m.chain_id = chain_id;
                m.gas_limit = gas_limit;
                if gas_price.is_none() {
                    m.gas_price = self.gas_price().unwrap();
                }
                TransactionMessage::EIP2930(m)
            }
            Some(TransactionMessage::EIP1559(mut m)) => {
                m.nonce = nonce;
                m.chain_id = chain_id;
                m.gas_limit = gas_limit;
                if max_fee_per_gas.is_none() {
                    m.max_fee_per_gas = self.gas_price().unwrap();
                }
                TransactionMessage::EIP1559(m)
            }
            _ => {
                return Err(Error::Custom(String::from(
                    "invalid transaction parameters",
                )));
            }
        };

        let signed = sign(from, message).unwrap();

        let encoded = hex::encode(signed.encode());

        Ok(encoded)
    }

    fn send_transaction(&self, request: TransactionRequest) -> RpcResult<String> {
        debug!(target:"rpc", "[send_transaction] Sending transaction: {:?}", request);

        let signed = self.sign_transaction(request)?;

        let hash = self.send_raw_transaction(signed.as_str())?;
        debug!(target:"rpc", "[send_transaction] signed: {:?}", hash);

        Ok(hash)
    }

    fn send_raw_transaction(&self, tx: &str) -> RpcResult<String> {
        debug!(target:"rpc", "[send_raw_transaction] Sending raw transaction: {:?}", tx);
        let raw_tx = tx.strip_prefix("0x").unwrap_or(tx);
        let hex =
            hex::decode(raw_tx).map_err(|e| Error::Custom(format!("Eror decoding TX {e:?}")))?;

        match ain_cpp_imports::publish_eth_transaction(hex) {
            Ok(res_string) => {
                if res_string.is_empty() {
                    let signed_tx = SignedTx::try_from(raw_tx)
                        .map_err(|e| Error::Custom(format!("TX error {e:?}")))?;

                    debug!(target:"rpc",
                        "[send_raw_transaction] signed_tx sender : {:#x}",
                        signed_tx.sender
                    );
                    debug!(target:"rpc",
                        "[send_raw_transaction] signed_tx nonce : {:#x}",
                        signed_tx.nonce()
                    );
                    debug!(target:"rpc",
                        "[send_raw_transaction] transaction hash : {:#x}",
                        signed_tx.transaction.hash()
                    );

                    Ok(format!("{:#x}", signed_tx.transaction.hash()))
                } else {
                    debug!(target:"rpc", "[send_raw_transaction] Could not publish raw transaction: {tx} reason: {res_string}");
                    Err(Error::Custom(format!(
                        "Could not publish raw transaction: {tx} reason: {res_string}"
                    )))
                }
            }
            Err(e) => {
                debug!(target:"rpc", "[send_raw_transaction] Error publishing TX {e:?}");
                Err(Error::Custom(format!("Error publishing TX {e:?}")))
            }
        }
    }

    fn get_transaction_count(
        &self,
        address: H160,
        block_number: Option<BlockNumber>,
    ) -> RpcResult<U256> {
        debug!(target:"rpc", "Getting transaction count for address: {:?}", address);
        let block_number = self.block_number_to_u256(block_number)?;
        let nonce = self
            .handler
            .core
            .get_nonce(address, block_number)
            .map_err(|e| {
                Error::Custom(format!("Error getting address transaction count : {e:?}"))
            })?;

        debug!(target:"rpc", "Count: {:#?}", nonce);
        Ok(nonce)
    }

    fn estimate_gas(
        &self,
        input: CallRequest,
        block_number: Option<BlockNumber>,
    ) -> RpcResult<U256> {
        let CallRequest {
            from,
            to,
            gas,
            value,
            data,
            access_list,
            ..
        } = input;

        let block_number = self.block_number_to_u256(block_number)?;
        let TxResponse { used_gas, .. } = self
            .handler
            .core
            .call(EthCallArgs {
                caller: from,
                to,
                value: value.unwrap_or_default(),
                data: &data.map(|d| d.0).unwrap_or_default(),
                gas_limit: gas.unwrap_or(MAX_GAS_PER_BLOCK).as_u64(),
                access_list: access_list.unwrap_or_default(),
                block_number,
            })
            .map_err(|e| Error::Custom(format!("Error calling EVM : {e:?}")))?;

        debug!(target:"rpc",  "estimateGas: {:#?} at block {:#x}", used_gas, block_number);
        Ok(U256::from(used_gas))
    }

    fn gas_price(&self) -> RpcResult<U256> {
        let gas_price = self.handler.block.get_legacy_fee();
        debug!(target:"rpc", "gasPrice: {:#?}", gas_price);
        Ok(gas_price)
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

    fn fee_history(
        &self,
        block_count: U256,
        first_block: U256,
        priority_fee_percentile: Vec<usize>,
    ) -> RpcResult<RpcFeeHistory> {
        Ok(RpcFeeHistory::from(self.handler.block.fee_history(
            block_count.as_usize(),
            first_block,
            priority_fee_percentile,
        )))
    }

    fn max_priority_fee_per_gas(&self) -> RpcResult<U256> {
        Ok(self.handler.block.suggested_priority_fee())
    }

    fn syncing(&self) -> RpcResult<SyncState> {
        let (current_native_height, highest_native_block) = ain_cpp_imports::get_sync_status()
            .map_err(|e| {
                Error::Custom(format!("ain_cpp_imports::get_sync_status error : {e:?}"))
            })?;

        if current_native_height == -1 {
            return Err(Error::Custom(String::from("Block index not available")));
        }

        match current_native_height != highest_native_block {
            true => {
                let current_block = self
                    .handler
                    .storage
                    .get_latest_block()
                    .map(|block| block.header.number)
                    .ok_or_else(|| Error::Custom(String::from("Unable to get current block")))?;

                let starting_block = self.handler.block.get_first_block_number();

                let highest_block = current_block + (highest_native_block - current_native_height);
                debug!("Highest native: {highest_native_block}\nCurrent native: {current_native_height}\nCurrent ETH: {current_block}\nHighest ETH: {highest_block}");

                Ok(SyncState::Syncing(SyncInfo {
                    starting_block,
                    current_block,
                    highest_block,
                }))
            }
            false => Ok(SyncState::Synced(false)),
        }
    }
    fn get_uncle_count_by_block_number(&self) -> RpcResult<U256> {
        Ok(Default::default())
    }

    fn get_uncle_count_by_block_hash(&self) -> RpcResult<U256> {
        Ok(Default::default())
    }

    fn get_uncle_by_block_number(&self) -> RpcResult<Option<bool>> {
        Ok(Default::default())
    }

    fn get_uncle_by_block_hash(&self) -> RpcResult<Option<bool>> {
        Ok(Default::default())
    }

    fn get_logs(&self, input: GetLogsRequest) -> RpcResult<Vec<LogResult>> {
        if let (Some(_), Some(_)) = (input.block_hash, input.to_block.or(input.from_block)) {
            return Err(Error::Custom(String::from(
                "cannot specify both blockHash and fromBlock/toBlock, choose one or the other",
            )));
        }

        let block_numbers = match input.block_hash {
            None => {
                // use fromBlock-toBlock
                let mut block_number = self.block_number_to_u256(input.from_block)?;
                let to_block_number = self.block_number_to_u256(input.to_block)?;
                let mut block_numbers = Vec::new();

                if block_number > to_block_number {
                    return Err(Error::Custom(format!(
                        "fromBlock ({}) > toBlock ({})",
                        format_u256(block_number),
                        format_u256(to_block_number)
                    )));
                }

                while block_number <= to_block_number {
                    block_numbers.push(block_number);
                    block_number += U256::one();
                }

                block_numbers
            }
            Some(block_hash) => {
                vec![
                    self.handler
                        .storage
                        .get_block_by_hash(&block_hash)
                        .ok_or_else(|| Error::Custom(String::from("Unable to find block hash")))?
                        .header
                        .number,
                ]
            }
        };

        Ok(block_numbers
            .into_iter()
            .flat_map(|block_number| {
                self.handler
                    .logs
                    .get_logs(&input.address, &input.topics, block_number)
                    .into_iter()
                    .map(LogResult::from)
            })
            .collect())
    }

    fn new_filter(&self, input: NewFilterRequest) -> RpcResult<U256> {
        let from_block_number = self.block_number_to_u256(input.from_block)?;
        let to_block_number = self.block_number_to_u256(input.to_block)?;

        if from_block_number > to_block_number {
            return Err(Error::Custom(format!(
                "fromBlock ({}) > toBlock ({})",
                format_u256(from_block_number),
                format_u256(to_block_number)
            )));
        }

        let current_block_height = match self.handler.storage.get_latest_block() {
            None => return Err(Error::Custom(String::from("Latest block unavailable"))),
            Some(block) => block.header.number,
        };

        Ok(self
            .handler
            .filters
            .create_logs_filter(
                input.address,
                input.topics,
                from_block_number,
                to_block_number,
                current_block_height,
            )
            .into())
    }

    fn new_block_filter(&self) -> RpcResult<U256> {
        Ok(self.handler.filters.create_block_filter().into())
    }

    fn get_filter_changes(&self, filter_id: U256) -> RpcResult<GetFilterChangesResult> {
        let filter = self
            .handler
            .filters
            .get_filter(filter_id.as_usize())
            .map_err(|e| Error::Custom(String::from(e)))?;

        let res = match filter {
            Filter::Logs(filter) => {
                let current_block_height = match self.handler.storage.get_latest_block() {
                    None => return Err(Error::Custom(String::from("Latest block unavailable"))),
                    Some(block) => block.header.number,
                };

                let logs = self
                    .handler
                    .logs
                    .get_logs_from_filter(filter)
                    .into_iter()
                    .map(LogResult::from)
                    .collect();

                self.handler
                    .filters
                    .update_last_block(filter_id.as_usize(), current_block_height)
                    .map_err(|e| Error::Custom(String::from(e)))?;

                GetFilterChangesResult::Logs(logs)
            }
            Filter::NewBlock(_) => GetFilterChangesResult::NewBlock(
                self.handler
                    .filters
                    .get_entries_from_filter(filter_id.as_usize())
                    .map_err(|e| Error::Custom(String::from(e)))?,
            ),
            Filter::NewPendingTransactions(_) => GetFilterChangesResult::NewPendingTransactions(
                self.handler
                    .filters
                    .get_entries_from_filter(filter_id.as_usize())
                    .map_err(|e| Error::Custom(String::from(e)))?,
            ),
        };

        Ok(res)
    }

    fn uninstall_filter(&self, filter_id: U256) -> RpcResult<bool> {
        Ok(self.handler.filters.delete_filter(filter_id.as_usize()))
    }

    fn get_filter_logs(&self, filter_id: U256) -> RpcResult<Vec<LogResult>> {
        let filter = self
            .handler
            .filters
            .get_filter(filter_id.as_usize())
            .map_err(|e| Error::Custom(String::from(e)))?;

        match filter {
            Filter::Logs(filter) => {
                // use fromBlock-toBlock
                let mut block_number = filter.from_block;
                let to_block_number = filter.to_block;
                let mut block_numbers = Vec::new();

                if block_number > to_block_number {
                    return Err(Error::Custom(format!(
                        "fromBlock ({}) > toBlock ({})",
                        format_u256(block_number),
                        format_u256(to_block_number)
                    )));
                }

                while block_number <= to_block_number {
                    block_numbers.push(block_number);
                    block_number += U256::one();
                }

                Ok(block_numbers
                    .into_iter()
                    .flat_map(|block_number| {
                        self.handler
                            .logs
                            .get_logs(&filter.address, &filter.topics, block_number)
                            .into_iter()
                            .map(LogResult::from)
                    })
                    .collect())
            }
            _ => Err(Error::Custom(String::from("Filter is not a log filter"))),
        }
    }

    fn new_pending_transaction_filter(&self) -> RpcResult<U256> {
        Ok(self.handler.filters.create_transaction_filter().into())
    }
}

fn sign(
    address: H160,
    message: TransactionMessage,
) -> Result<TransactionV2, Box<dyn std::error::Error>> {
    debug!("sign address {:#x}", address);
    let key_id = address.as_fixed_bytes().to_owned();
    let priv_key = get_eth_priv_key(key_id).unwrap();
    let secret_key = SecretKey::parse(&priv_key).unwrap();

    match message {
        TransactionMessage::Legacy(m) => {
            let signing_message = libsecp256k1::Message::parse_slice(&m.hash()[..])
                .map_err(|_| Error::Custom(String::from("invalid signing message")))?;
            let (signature, recid) = libsecp256k1::sign(&signing_message, &secret_key);
            let v = match m.chain_id {
                None => 27 + recid.serialize() as u64,
                Some(chain_id) => 2 * chain_id + 35 + recid.serialize() as u64,
            };
            let rs = signature.serialize();
            let r = H256::from_slice(&rs[0..32]);
            let s = H256::from_slice(&rs[32..64]);

            Ok(TransactionV2::Legacy(ethereum::LegacyTransaction {
                nonce: m.nonce,
                value: m.value,
                input: m.input,
                signature: ethereum::TransactionSignature::new(v, r, s).ok_or_else(|| {
                    Error::Custom(String::from("signer generated invalid signature"))
                })?,
                gas_price: m.gas_price,
                gas_limit: m.gas_limit,
                action: m.action,
            }))
        }
        TransactionMessage::EIP2930(m) => {
            let signing_message = libsecp256k1::Message::parse_slice(&m.hash()[..])
                .map_err(|_| Error::Custom(String::from("invalid signing message")))?;
            let (signature, recid) = libsecp256k1::sign(&signing_message, &secret_key);
            let rs = signature.serialize();
            let r = H256::from_slice(&rs[0..32]);
            let s = H256::from_slice(&rs[32..64]);

            Ok(TransactionV2::EIP2930(ethereum::EIP2930Transaction {
                chain_id: m.chain_id,
                nonce: m.nonce,
                gas_price: m.gas_price,
                gas_limit: m.gas_limit,
                action: m.action,
                value: m.value,
                input: m.input.clone(),
                access_list: m.access_list,
                odd_y_parity: recid.serialize() != 0,
                r,
                s,
            }))
        }
        TransactionMessage::EIP1559(m) => {
            let signing_message = libsecp256k1::Message::parse_slice(&m.hash()[..])
                .map_err(|_| Error::Custom(String::from("invalid signing message")))?;
            let (signature, recid) = libsecp256k1::sign(&signing_message, &secret_key);
            let rs = signature.serialize();
            let r = H256::from_slice(&rs[0..32]);
            let s = H256::from_slice(&rs[32..64]);

            Ok(TransactionV2::EIP1559(ethereum::EIP1559Transaction {
                chain_id: m.chain_id,
                nonce: m.nonce,
                max_priority_fee_per_gas: m.max_priority_fee_per_gas,
                max_fee_per_gas: m.max_fee_per_gas,
                gas_limit: m.gas_limit,
                action: m.action,
                value: m.value,
                input: m.input.clone(),
                access_list: m.access_list,
                odd_y_parity: recid.serialize() != 0,
                r,
                s,
            }))
        }
    }
}
