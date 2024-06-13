use std::{collections::BTreeMap, convert::Into, str::FromStr, sync::Arc};

use ain_cpp_imports::get_eth_priv_key;
use ain_evm::{
    bytes::Bytes,
    core::EthCallArgs,
    evm::EVMServices,
    executor::TxResponse,
    filters::FilterCriteria,
    storage::traits::{BlockStorage, ReceiptStorage, TransactionStorage},
    transaction::SignedTx,
};
use ethereum::{EnvelopedEncodable, TransactionV2};
use ethereum_types::{H160, H256, U256};
use evm::{Config, ExitError, ExitReason};
use jsonrpsee::{
    core::{Error, RpcResult},
    proc_macros::rpc,
};
use libsecp256k1::SecretKey;
use log::trace;

use crate::{
    block::{BlockNumber, RpcBlock, RpcFeeHistory},
    call_request::{override_to_overlay, AccessListResult, CallRequest, CallStateOverride},
    codegen::types::EthTransactionInfo,
    errors::{to_custom_err, RPCError},
    filters::{GetFilterChangesResult, NewFilterRequest},
    logs::{GetLogsRequest, LogRequestTopics, LogResult},
    receipt::ReceiptResult,
    sync::{SyncInfo, SyncState},
    transaction_request::{TransactionMessage, TransactionRequest},
    utils::{format_h256, format_u256, try_get_reverted_error_or_default},
};

use super::common::get_block;

#[rpc(server, client, namespace = "eth")]
pub trait MetachainRPC {
    // ----------------------------------------
    // Client
    // ----------------------------------------

    /// Makes a call to the Ethereum node without creating a transaction on the blockchain.
    /// Returns the output data as a hexadecimal string.
    #[method(name = "call")]
    fn call(
        &self,
        input: CallRequest,
        block_number: Option<BlockNumber>,
        state_overrides: Option<BTreeMap<H160, CallStateOverride>>,
    ) -> RpcResult<Bytes>;

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
    fn get_work(&self) -> RpcResult<Vec<String>>;

    /// Submits a proof of work solution to the node.
    /// Always returns false
    #[method(name = "submitWork")]
    fn submit_work(&self, nonce: String, hash: String, digest: String) -> RpcResult<bool>;

    /// Retrieves the current hash rate of the node.
    /// Always returns 0x0
    #[method(name = "hashrate")]
    fn hash_rate(&self) -> RpcResult<String>;

    /// Submit mining hashrate.
    /// Always returns false
    #[method(name = "submitHashrate")]
    fn submit_hashrate(&self, hashrate: String, id: String) -> RpcResult<bool>;

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
        index: U256,
    ) -> RpcResult<Option<EthTransactionInfo>>;

    /// Retrieves a specific transaction, identified by the block number and transaction index.
    #[method(name = "getTransactionByBlockNumberAndIndex")]
    fn get_transaction_by_block_number_and_index(
        &self,
        block_number: U256,
        index: U256,
    ) -> RpcResult<Option<EthTransactionInfo>>;

    /// Retrieves the list of pending transactions.
    #[method(name = "pendingTransactions")]
    fn get_pending_transaction(&self) -> RpcResult<Vec<EthTransactionInfo>>;

    /// Retrieves the receipt of a specific transaction, identified by its hash.
    #[method(name = "getTransactionReceipt")]
    fn get_receipt(&self, hash: H256) -> RpcResult<Option<ReceiptResult>>;

    /// Create access list from a specified transaction call context.
    #[method(name = "createAccessList")]
    fn create_access_list(
        &self,
        call: CallRequest,
        block_number: Option<BlockNumber>,
    ) -> RpcResult<AccessListResult>;

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
        state_overrides: Option<BTreeMap<H160, CallStateOverride>>,
    ) -> RpcResult<U256>;

    /// Returns current gas_price.
    #[method(name = "gasPrice")]
    fn gas_price(&self) -> RpcResult<U256>;

    #[method(name = "feeHistory")]
    fn fee_history(
        &self,
        block_count: U256,
        newest_block: BlockNumber,
        reward_percentile: Vec<i64>,
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
    const CONFIG: Config = Config::shanghai();

    #[must_use]
    pub fn new(handler: Arc<EVMServices>) -> Self {
        Self { handler }
    }
}

impl MetachainRPCServer for MetachainRPCModule {
    fn call(
        &self,
        call: CallRequest,
        block_number: Option<BlockNumber>,
        state_overrides: Option<BTreeMap<H160, CallStateOverride>>,
    ) -> RpcResult<Bytes> {
        trace!(target:"rpc",  "Call, input {:#?}", call);

        let caller = call.from.unwrap_or_default();
        let byte_data = call.get_data()?;
        let data = byte_data.0.as_slice();

        // Get gas
        let block_gas_limit = ain_cpp_imports::get_attribute_values(None).block_gas_limit;
        let gas_limit = u64::try_from(call.gas.unwrap_or(U256::from(block_gas_limit)))
            .map_err(to_custom_err)?;

        let block = get_block(&self.handler.storage, block_number)?;
        let block_base_fee = block.header.base_fee;
        let gas_price = call.get_effective_gas_price()?.unwrap_or(block_base_fee);

        let TxResponse {
            data, exit_reason, ..
        } = self
            .handler
            .core
            .call(
                EthCallArgs {
                    caller,
                    to: call.to,
                    value: call.value.unwrap_or_default(),
                    data,
                    gas_limit,
                    gas_price,
                    access_list: call.access_list.unwrap_or_default(),
                    block_number: block.header.number,
                },
                state_overrides.map(override_to_overlay),
            )
            .map_err(RPCError::EvmError)?;

        match exit_reason {
            ExitReason::Succeed(_) => Ok(Bytes(data)),
            ExitReason::Error(e) => Err(Error::Custom(format!("exit error {e:?}"))),
            ExitReason::Revert(_) => {
                let revert_msg = try_get_reverted_error_or_default(&data);
                let encoded_data = format!("0x{}", hex::encode(data));
                Err(RPCError::RevertError(revert_msg, encoded_data).into())
            }
            ExitReason::Fatal(e) => Err(Error::Custom(format!("fatal error {e:?}"))),
        }
    }

    fn accounts(&self) -> RpcResult<Vec<String>> {
        let accounts = ain_cpp_imports::get_accounts()
            .map_err(|e| to_custom_err(format!("Error getting accounts {e}")))?;
        Ok(accounts)
    }

    // State RPC

    fn get_balance(&self, address: H160, block_number: Option<BlockNumber>) -> RpcResult<U256> {
        let block = get_block(&self.handler.storage, block_number)?;
        trace!(target:"rpc",
            "Getting balance for address: {:?} at block : {} ",
            address, block.header.number
        );
        let balance = self
            .handler
            .core
            .get_balance(address, block.header.state_root)
            .map_err(to_custom_err)?;

        trace!(target:"rpc", "Address: {:?} balance : {} ", address, balance);
        Ok(balance)
    }

    fn get_code(&self, address: H160, block_number: Option<BlockNumber>) -> RpcResult<String> {
        let block = get_block(&self.handler.storage, block_number)?;

        trace!(target:"rpc",
            "Getting code for address: {:?} at block : {}",
            address, block.header.number
        );

        let code = self
            .handler
            .core
            .get_code(address, block.header.state_root)
            .map_err(to_custom_err)?;

        trace!(target:"rpc", "code : {:?} for address {address:?}", code);
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
        let block = get_block(&self.handler.storage, block_number)?;
        trace!(target:"rpc",
            "Getting storage for address: {:?}, at position {:?}, for block {}",
            address, position, block.header.number
        );

        self.handler
            .core
            .get_storage_at(address, position, block.header.state_root)
            .map_err(to_custom_err)?
            .map_or(Ok(H256::default()), |storage| {
                Ok(H256::from_slice(&storage))
            })
    }

    fn get_block_by_hash(
        &self,
        hash: H256,
        full_transactions: Option<bool>,
    ) -> RpcResult<Option<RpcBlock>> {
        trace!("Getting block by hash {:#x}", hash);
        self.handler
            .storage
            .get_block_by_hash(&hash)
            .map_err(to_custom_err)?
            .map_or(Ok(None), |block| {
                Ok(Some(RpcBlock::from_block_with_tx(
                    block,
                    full_transactions.unwrap_or_default(),
                )))
            })
    }

    fn chain_id(&self) -> RpcResult<String> {
        let chain_id = ain_cpp_imports::get_chain_id().map_err(RPCError::Error)?;
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
            .map_err(to_custom_err)?
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
        let block_number = get_block(&self.handler.storage, Some(block_number))?
            .header
            .number;
        trace!(target:"rpc", "Getting block by number : {}", block_number);
        self.handler
            .storage
            .get_block_by_number(&block_number)
            .map_err(to_custom_err)?
            .map_or(Ok(None), |block| {
                Ok(Some(RpcBlock::from_block_with_tx(
                    block,
                    full_transactions.unwrap_or_default(),
                )))
            })
    }

    fn mining(&self) -> RpcResult<bool> {
        ain_cpp_imports::is_mining().map_err(|e| RPCError::Error(e).into())
    }

    fn get_transaction_by_hash(&self, hash: H256) -> RpcResult<Option<EthTransactionInfo>> {
        self.handler
            .storage
            .get_transaction_by_hash(&hash)
            .map_err(to_custom_err)?
            .map_or(Ok(None), |tx| {
                let mut transaction_info: EthTransactionInfo =
                    tx.try_into().map_err(to_custom_err)?;

                // TODO: Improve efficiency by indexing the block_hash, block_number, and transaction_index fields.
                // Temporary workaround: Makes an additional call to get_receipt where these fields are available.
                if let Some(receipt) = self
                    .handler
                    .storage
                    .get_receipt(&hash)
                    .map_err(to_custom_err)?
                {
                    transaction_info.block_hash = Some(format_h256(receipt.block_hash));
                    transaction_info.block_number = Some(format_u256(receipt.block_number));
                    transaction_info.transaction_index =
                        Some(format_u256(U256::from(receipt.tx_index)));
                    transaction_info.gas_price = format_u256(receipt.effective_gas_price)
                }

                Ok(Some(transaction_info))
            })
    }

    fn get_pending_transaction(&self) -> RpcResult<Vec<EthTransactionInfo>> {
        ain_cpp_imports::get_pool_transactions()
            .map(|txs| {
                txs.into_iter()
                    .flat_map(|tx| EthTransactionInfo::try_from(tx.data.as_str()))
                    .map(EthTransactionInfo::into_pending_transaction_info)
                    .collect()
            })
            .map_err(to_custom_err)
    }

    fn get_transaction_by_block_hash_and_index(
        &self,
        hash: H256,
        index: U256,
    ) -> RpcResult<Option<EthTransactionInfo>> {
        self.handler
            .storage
            .get_transaction_by_block_hash_and_index(
                &hash,
                index.try_into().map_err(to_custom_err)?,
            )
            .map_err(to_custom_err)?
            .map_or(Ok(None), |tx| {
                let tx_hash = &tx.hash();
                let mut transaction_info: EthTransactionInfo =
                    tx.try_into().map_err(to_custom_err)?;

                // TODO: Improve efficiency by indexing the block_hash, block_number, and transaction_index fields.
                // Temporary workaround: Makes an additional call to get_receipt where these fields are available.
                if let Some(receipt) = self
                    .handler
                    .storage
                    .get_receipt(tx_hash)
                    .map_err(to_custom_err)?
                {
                    transaction_info.block_hash = Some(format_h256(receipt.block_hash));
                    transaction_info.block_number = Some(format_u256(receipt.block_number));
                    transaction_info.transaction_index =
                        Some(format_u256(U256::from(receipt.tx_index)));
                    transaction_info.gas_price = format_u256(receipt.effective_gas_price)
                }

                Ok(Some(transaction_info))
            })
    }

    fn get_transaction_by_block_number_and_index(
        &self,
        number: U256,
        index: U256,
    ) -> RpcResult<Option<EthTransactionInfo>> {
        self.handler
            .storage
            .get_transaction_by_block_number_and_index(
                &number,
                index.try_into().map_err(to_custom_err)?,
            )
            .map_err(to_custom_err)?
            .map_or(Ok(None), |tx| {
                let tx_hash = &tx.hash();
                let mut transaction_info: EthTransactionInfo =
                    tx.try_into().map_err(to_custom_err)?;

                // TODO: Improve efficiency by indexing the block_hash, block_number, and transaction_index fields.
                // Temporary workaround: Makes an additional call to get_receipt where these fields are available.
                if let Some(receipt) = self
                    .handler
                    .storage
                    .get_receipt(tx_hash)
                    .map_err(to_custom_err)?
                {
                    transaction_info.block_hash = Some(format_h256(receipt.block_hash));
                    transaction_info.block_number = Some(format_u256(receipt.block_number));
                    transaction_info.transaction_index =
                        Some(format_u256(U256::from(receipt.tx_index)));
                    transaction_info.gas_price = format_u256(receipt.effective_gas_price)
                }

                Ok(Some(transaction_info))
            })
    }

    fn get_block_transaction_count_by_hash(&self, hash: H256) -> RpcResult<usize> {
        self.handler
            .storage
            .get_block_by_hash(&hash)
            .map_err(to_custom_err)?
            .map_or(Ok(0), |b| Ok(b.transactions.len()))
    }

    fn get_block_transaction_count_by_number(&self, block_number: BlockNumber) -> RpcResult<usize> {
        let block_number = get_block(&self.handler.storage, Some(block_number))?
            .header
            .number;
        self.handler
            .storage
            .get_block_by_number(&block_number)
            .map_err(to_custom_err)?
            .map_or(Ok(0), |b| Ok(b.transactions.len()))
    }

    fn sign_transaction(&self, request: TransactionRequest) -> RpcResult<String> {
        trace!(target:"rpc", "Signing transaction: {:?}", request);

        let from = match request.from {
            Some(from) => from,
            None => {
                let accounts = self.accounts()?;

                match accounts.first() {
                    Some(account) => H160::from_str(account.as_str())
                        .map_err(|_| to_custom_err("Wrong from address"))?,
                    None => return Err(to_custom_err("from is not available")),
                }
            }
        };
        trace!(target:"rpc", "[sign_transaction] from: {:?}", from);

        let chain_id = ain_cpp_imports::get_chain_id().map_err(RPCError::Error)?;
        let Ok(state_root) = self.handler.core.get_latest_state_root() else {
            return Err(RPCError::StateRootNotFound.into());
        };
        let nonce = match request.nonce {
            Some(nonce) => nonce,
            None => self
                .handler
                .core
                .get_next_account_nonce(from, state_root)
                .map_err(RPCError::EvmError)?,
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
                    m.gas_price = self.gas_price()?;
                }
                TransactionMessage::Legacy(m)
            }
            Some(TransactionMessage::EIP2930(mut m)) => {
                m.nonce = nonce;
                m.chain_id = chain_id;
                m.gas_limit = gas_limit;
                if gas_price.is_none() {
                    m.gas_price = self.gas_price()?;
                }
                TransactionMessage::EIP2930(m)
            }
            Some(TransactionMessage::EIP1559(mut m)) => {
                m.nonce = nonce;
                m.chain_id = chain_id;
                m.gas_limit = gas_limit;
                if max_fee_per_gas.is_none() {
                    m.max_fee_per_gas = self.gas_price()?;
                }
                TransactionMessage::EIP1559(m)
            }
            _ => {
                return Err(RPCError::InvalidTransactionMessage.into());
            }
        };

        let signed = sign(from, message)?;
        let encoded = hex::encode(signed.encode());
        Ok(encoded)
    }

    fn send_transaction(&self, request: TransactionRequest) -> RpcResult<String> {
        trace!(target:"rpc", "Sending transaction: {:?}", request);
        let signed = self.sign_transaction(request)?;
        let hash = self.send_raw_transaction(signed.as_str())?;

        trace!(target:"rpc", "[send_transaction] signed: {:?}", hash);
        Ok(hash)
    }

    fn send_raw_transaction(&self, tx: &str) -> RpcResult<String> {
        trace!(target:"rpc", "Sending raw transaction: {:?}", tx);
        let raw_tx = tx.strip_prefix("0x").unwrap_or(tx);
        let hex = hex::decode(raw_tx).map_err(to_custom_err)?;

        let res_string = ain_cpp_imports::publish_eth_transaction(hex).map_err(RPCError::Error)?;
        if res_string.is_empty() {
            let signed_tx: SignedTx = self
                .handler
                .core
                .tx_cache
                .try_get_or_create(raw_tx)
                .map_err(RPCError::EvmError)?;

            trace!(target:"rpc",
                "[send_raw_transaction] signed_tx sender : {:#x}",
                signed_tx.sender
            );
            trace!(target:"rpc",
                "[send_raw_transaction] signed_tx nonce : {:#x}",
                signed_tx.nonce()
            );
            trace!(target:"rpc",
                "[send_raw_transaction] transaction hash : {:#x}",
                signed_tx.hash()
            );

            if !self
                .handler
                .core
                .store_account_nonce(signed_tx.sender, signed_tx.nonce())
            {
                return Err(RPCError::NonceCacheError.into());
            }
            Ok(format!("{:#x}", signed_tx.hash()))
        } else {
            trace!(target:"rpc", "[send_raw_transaction] Could not publish raw transaction: {tx} reason: {res_string}");
            Err(Error::Custom(format!(
                "Could not publish raw transaction: {tx} reason: {res_string}"
            )))
        }
    }

    fn get_transaction_count(
        &self,
        address: H160,
        block_number: Option<BlockNumber>,
    ) -> RpcResult<U256> {
        trace!(target:"rpc", "Getting transaction count for address: {:?}", address);
        let block = get_block(&self.handler.storage, block_number)?;
        let nonce = self
            .handler
            .core
            .get_nonce(address, block.header.state_root)
            .map_err(to_custom_err)?;

        trace!(target:"rpc", "Count: {:#?}", nonce);
        Ok(nonce)
    }

    /// Estimate returns the lowest possible gas limit that allows the transaction to
    /// run successfully with the provided context options. It returns an error if the
    /// transaction would always revert, or if there are unexpected failures.
    ///
    /// To configure the gas estimation error ratio, set-evmestimategaserrorratio=n on
    /// startup. Otherwise, the default parameter is set at 15% error ratio.
    ///
    /// Ref: https://github.com/ethereum/go-ethereum/blob/e2778cd59f04f7587c9aa5983282074026ff6684/eth/gasestimator/gasestimator.go
    fn estimate_gas(
        &self,
        call: CallRequest,
        block_number: Option<BlockNumber>,
        state_overrides: Option<BTreeMap<H160, CallStateOverride>>,
    ) -> RpcResult<U256> {
        trace!(target:"rpc",  "Estimate gas, input {:#?}", call);

        let caller = call.from.unwrap_or_default();
        let byte_data = call.get_data()?;
        let data = byte_data.0.as_slice();
        let overlay = state_overrides.map(override_to_overlay);

        let block_gas_limit = ain_cpp_imports::get_attribute_values(None).block_gas_limit;
        let call_gas = u64::try_from(call.gas.unwrap_or(U256::from(block_gas_limit)))
            .map_err(to_custom_err)?;

        // Determine the highest gas limit can be used during the estimation.
        let mut hi = block_gas_limit;
        if call_gas >= Self::CONFIG.gas_transaction_call {
            hi = call_gas;
        }

        // Get block base fee
        let block = get_block(&self.handler.storage, block_number)?;
        let block_base_fee = block.header.base_fee;

        // Normalize the max fee per gas the call is willing to spend.
        let fee_cap = call.get_effective_gas_price()?;

        // Recap the highest gas allowance with account's balance if gas price
        if let Some(cap) = fee_cap {
            if call.from.is_some() {
                let balance = if let Some(balance) = overlay
                    .as_ref()
                    .and_then(|o| o.get_account(&caller).map(|acc| acc.balance))
                {
                    balance
                } else {
                    self.handler
                        .core
                        .get_balance(caller, block.header.state_root)
                        .map_err(to_custom_err)?
                };
                let mut available = balance;
                if let Some(value) = call.value {
                    if balance < value {
                        return Err(RPCError::InsufficientFunds.into());
                    }
                    available = balance.checked_sub(value).ok_or(RPCError::ValueUnderflow)?;
                }

                let allowance = available.checked_div(cap).ok_or(RPCError::DivideError)?;
                trace!(target:"rpc",  "[estimate_gas] allowance: {:#?}", allowance);

                if let Ok(allowance) = u64::try_from(allowance) {
                    if hi > allowance {
                        trace!("[estimate_gas] gas estimation capped by limited funds. original: {:#?}, balance: {:#?}, feecap: {:#?}, fundable: {:#?}", hi, balance, fee_cap, allowance);
                        hi = allowance;
                    }
                }
            }
        }
        let fee_cap = fee_cap.unwrap_or(block_base_fee);

        // If the transaction is plain value transfer, short circuit estimation and directly
        // try 21_000. Returning 21_000 without any execution is dangerous as some tx field
        // combos might bump the price up even for plain transfers (e.g. unused access list
        // items). Ever so slightly wasteful, but safer overall.
        if data.is_empty() {
            if let Some(to) = call.to {
                if overlay.as_ref().and_then(|o| o.get_code(&to)).is_none()
                    && self
                        .handler
                        .core
                        .get_code(to, block.header.state_root)
                        .map_err(to_custom_err)?
                        .is_none()
                {
                    let tx_response = self
                        .handler
                        .core
                        .call(
                            EthCallArgs {
                                caller,
                                to: Some(to),
                                value: call.value.unwrap_or_default(),
                                data,
                                gas_limit: call_gas,
                                gas_price: fee_cap,
                                access_list: call.access_list.clone().unwrap_or_default(),
                                block_number: block.header.number,
                            },
                            overlay.clone(),
                        )
                        .map_err(RPCError::EvmError)?;
                    if let ExitReason::Succeed(_) = tx_response.exit_reason {
                        return Ok(U256::from(tx_response.used_gas));
                    }
                }
            }
        }

        // Create a helper to check if a gas allowance results in an executable transaction
        // Returns: (tx execution failure flag, out of gas failure flag, used gas)
        let executable = |gas_limit: u64| -> Result<(bool, bool, u64), Error> {
            // Consensus error, this means the provided message call or transaction will
            // never be accepted no matter how much gas it is assigned. Return the error
            // directly, don't struggle any more
            let tx_response = self
                .handler
                .core
                .call(
                    EthCallArgs {
                        caller,
                        to: call.to,
                        value: call.value.unwrap_or_default(),
                        data,
                        gas_limit,
                        gas_price: fee_cap,
                        access_list: call.access_list.clone().unwrap_or_default(),
                        block_number: block.header.number,
                    },
                    overlay.clone(),
                )
                .map_err(RPCError::EvmError)?;

            match tx_response.exit_reason {
                ExitReason::Error(ExitError::OutOfGas) => Ok((true, true, tx_response.used_gas)),
                ExitReason::Succeed(_) => Ok((false, false, tx_response.used_gas)),
                _ => Ok((true, false, tx_response.used_gas)),
            }
        };

        // We first execute the transaction at the highest allowable gas limit, since
        // if this fails we can return error immediately.
        let (failed, out_of_gas, used_gas) = executable(hi)?;
        if failed {
            if !out_of_gas {
                return Err(RPCError::TxExecutionFailed.into());
            } else {
                return Err(RPCError::GasCapTooLow(hi).into());
            }
        }

        // For almost any transaction, the gas consumed by the unconstrained execution
        // above lower-bounds the gas limit required for it to succeed. One exception
        // is those that explicitly check gas remaining in order to execute within a
        // given limit, but we probably don't want to return the lowest possible gas
        // limit for these cases anyway.
        let mut lo = used_gas.saturating_sub(1u64);
        while lo + 1 < hi {
            // Safe, since highest gas limit possible is set at BLOCK_GAS_LIMIT
            let diff_percentage = ((hi.saturating_sub(lo) as f64) / (hi as f64) * 100f64) as u64;
            if diff_percentage < ain_cpp_imports::get_estimate_gas_error_ratio() {
                break;
            }

            let sum = hi.checked_add(lo).ok_or(RPCError::ValueOverflow)?;
            let mut mid = sum.checked_div(2u64).ok_or(RPCError::DivideError)?;

            // Most txs don't need much higher gas limit than their gas used, and most txs don't
            // require near the full block limit of gas, so the selection of where to bisect the
            // range here is skewed to favor the low side.
            if mid > lo.saturating_mul(2u64) {
                mid = lo * 2;
            }

            let (failed, ..) = executable(mid)?;
            if failed {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        Ok(U256::from(hi))
    }

    fn gas_price(&self) -> RpcResult<U256> {
        self.handler
            .block
            .suggest_legacy_fee()
            .map_err(to_custom_err)
    }

    fn get_receipt(&self, hash: H256) -> RpcResult<Option<ReceiptResult>> {
        self.handler
            .storage
            .get_receipt(&hash)
            .map_err(to_custom_err)?
            .map_or(Ok(None), |receipt| Ok(Some(ReceiptResult::from(receipt))))
    }

    fn get_work(&self) -> RpcResult<Vec<String>> {
        Ok(vec![
            "0x0000000000000000000000000000000000000000000000000000000000000000".to_string(),
            "0x0000000000000000000000000000000000000000000000000000000000000000".to_string(),
            "0x0000000000000000000000000000000000000000000000000000000000000000".to_string(),
        ])
    }

    fn create_access_list(
        &self,
        call: CallRequest,
        block_number: Option<BlockNumber>,
    ) -> RpcResult<AccessListResult> {
        let caller = call.from.unwrap_or_default();
        let byte_data = call.get_data()?;
        let data = byte_data.0.as_slice();

        // Get gas
        let block_gas_limit = ain_cpp_imports::get_attribute_values(None).block_gas_limit;
        let gas_limit = u64::try_from(call.gas.unwrap_or(U256::from(block_gas_limit)))
            .map_err(to_custom_err)?;

        let block = get_block(&self.handler.storage, block_number)?;
        let block_base_fee = block.header.base_fee;
        let gas_price = call.get_effective_gas_price()?.unwrap_or(block_base_fee);

        let res = self
            .handler
            .tracer
            .create_access_list(EthCallArgs {
                caller,
                to: call.to,
                value: call.value.unwrap_or_default(),
                data,
                gas_limit,
                gas_price,
                access_list: call.access_list.unwrap_or_default(),
                block_number: block.header.number,
            })
            .map_err(RPCError::EvmError)?
            .into();
        Ok(res)
    }

    fn submit_work(&self, _nonce: String, _hash: String, _digest: String) -> RpcResult<bool> {
        Ok(false)
    }

    fn submit_hashrate(&self, _hashrate: String, _id: String) -> RpcResult<bool> {
        Ok(false)
    }

    fn fee_history(
        &self,
        block_count: U256,
        newest_block: BlockNumber,
        reward_percentile: Vec<i64>,
    ) -> RpcResult<RpcFeeHistory> {
        let highest_block_number = get_block(&self.handler.storage, Some(newest_block))?
            .header
            .number;
        let attrs = ain_cpp_imports::get_attribute_values(None);

        let fee_history = self
            .handler
            .block
            .fee_history(
                block_count,
                highest_block_number,
                reward_percentile,
                attrs.block_gas_target_factor,
            )
            .map_err(RPCError::EvmError)?;

        Ok(RpcFeeHistory::from(fee_history))
    }

    fn max_priority_fee_per_gas(&self) -> RpcResult<U256> {
        self.handler
            .block
            .suggest_priority_fee()
            .map_err(to_custom_err)
    }

    fn syncing(&self) -> RpcResult<SyncState> {
        let (current_native_height, highest_native_block) =
            ain_cpp_imports::get_sync_status().map_err(RPCError::Error)?;

        if current_native_height == -1 {
            return Err(to_custom_err("Block index not available"));
        }

        match current_native_height != highest_native_block {
            true => {
                let current_block = self
                    .handler
                    .storage
                    .get_latest_block()
                    .map_err(to_custom_err)?
                    .map(|block| block.header.number)
                    .ok_or(RPCError::BlockNotFound)?;

                let starting_block = self.handler.block.get_starting_block_number();

                let highest_block = current_block + (highest_native_block - current_native_height); // safe since current height cannot be higher than seen height
                trace!(target:"rpc", "Highest native: {highest_native_block}\nCurrent native: {current_native_height}\nCurrent ETH: {current_block}\nHighest ETH: {highest_block}");

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
        Ok(U256::default())
    }

    fn get_uncle_count_by_block_hash(&self) -> RpcResult<U256> {
        Ok(U256::default())
    }

    fn get_uncle_by_block_number(&self) -> RpcResult<Option<bool>> {
        Ok(None)
    }

    fn get_uncle_by_block_hash(&self) -> RpcResult<Option<bool>> {
        Ok(None)
    }

    fn get_logs(&self, input: GetLogsRequest) -> RpcResult<Vec<LogResult>> {
        let from_block = if input.from_block.is_some() {
            if let Some(BlockNumber::Num(block_num)) = input.from_block {
                // Allow future block number to be specified
                Some(U256::from(block_num))
            } else {
                Some(
                    get_block(&self.handler.storage, input.from_block)?
                        .header
                        .number,
                )
            }
        } else {
            None
        };
        let to_block = if input.to_block.is_some() {
            if let Some(BlockNumber::Num(block_num)) = input.to_block {
                // Allow future block number to be specified
                Some(U256::from(block_num))
            } else {
                Some(
                    get_block(&self.handler.storage, input.to_block)?
                        .header
                        .number,
                )
            }
        } else {
            None
        };
        let topics = input.topics.map(|topics| match topics {
            LogRequestTopics::VecOfHashes(inputs) => {
                inputs.iter().flatten().map(|input| vec![*input]).collect()
            }
            LogRequestTopics::VecOfHashVecs(inputs) => inputs
                .iter()
                .map(|hashes| hashes.iter().flatten().copied().collect())
                .collect(),
        });
        let curr_block = get_block(&self.handler.storage, Some(BlockNumber::Latest))?
            .header
            .number;
        let mut criteria = FilterCriteria {
            block_hash: input.block_hash,
            from_block,
            to_block,
            addresses: input.address,
            topics,
        };
        criteria
            .verify_criteria(curr_block)
            .map_err(RPCError::EvmError)?;
        let logs = self
            .handler
            .filters
            .get_logs_from_filter(&criteria)
            .map_err(RPCError::EvmError)?;
        Ok(logs.into_iter().map(|log| log.into()).collect())
    }

    fn new_filter(&self, input: NewFilterRequest) -> RpcResult<U256> {
        let from_block = if input.from_block.is_some() {
            if let Some(BlockNumber::Num(block_num)) = input.from_block {
                // Allow future block number to be specified
                Some(U256::from(block_num))
            } else {
                Some(
                    get_block(&self.handler.storage, input.from_block)?
                        .header
                        .number,
                )
            }
        } else {
            None
        };
        let to_block = if input.to_block.is_some() {
            if let Some(BlockNumber::Num(block_num)) = input.to_block {
                // Allow future block number to be specified
                Some(U256::from(block_num))
            } else {
                Some(
                    get_block(&self.handler.storage, input.to_block)?
                        .header
                        .number,
                )
            }
        } else {
            None
        };
        let topics = input.topics.map(|topics| match topics {
            LogRequestTopics::VecOfHashes(inputs) => {
                inputs.iter().flatten().map(|input| vec![*input]).collect()
            }
            LogRequestTopics::VecOfHashVecs(inputs) => inputs
                .iter()
                .map(|hashes| hashes.iter().flatten().copied().collect())
                .collect(),
        });
        let curr_block = get_block(&self.handler.storage, Some(BlockNumber::Latest))?
            .header
            .number;
        let mut criteria = FilterCriteria {
            from_block,
            to_block,
            addresses: input.address,
            topics,
            ..Default::default()
        };
        criteria
            .verify_criteria(curr_block)
            .map_err(RPCError::EvmError)?;
        Ok(self.handler.filters.create_log_filter(criteria).into())
    }

    fn new_block_filter(&self) -> RpcResult<U256> {
        Ok(self
            .handler
            .filters
            .create_block_filter()
            .map_err(RPCError::EvmError)?
            .into())
    }

    fn new_pending_transaction_filter(&self) -> RpcResult<U256> {
        Ok(self.handler.filters.create_tx_filter().into())
    }

    fn get_filter_changes(&self, filter_id: U256) -> RpcResult<GetFilterChangesResult> {
        let filter_id = usize::try_from(filter_id).map_err(to_custom_err)?;
        let curr_block = get_block(&self.handler.storage, Some(BlockNumber::Latest))?
            .header
            .number;
        let res = self
            .handler
            .filters
            .get_changes_from_filter_id(filter_id, curr_block)
            .map_err(RPCError::EvmError)?
            .into();
        Ok(res)
    }

    fn uninstall_filter(&self, filter_id: U256) -> RpcResult<bool> {
        let filter_id = usize::try_from(filter_id).map_err(to_custom_err)?;
        Ok(self.handler.filters.delete_filter(filter_id))
    }

    fn get_filter_logs(&self, filter_id: U256) -> RpcResult<Vec<LogResult>> {
        let filter_id = usize::try_from(filter_id).map_err(to_custom_err)?;
        let curr_block = get_block(&self.handler.storage, Some(BlockNumber::Latest))?
            .header
            .number;
        let logs = self
            .handler
            .filters
            .get_logs_from_filter_id(filter_id, curr_block)
            .map_err(RPCError::EvmError)?;
        Ok(logs.into_iter().map(|log| log.into()).collect())
    }
}

fn sign(address: H160, message: TransactionMessage) -> RpcResult<TransactionV2> {
    trace!(target: "rpc", "sign address {:#x}", address);
    let priv_key = get_eth_priv_key(address.to_fixed_bytes())
        .map_err(|_| to_custom_err("Invalid private key"))?;
    let secret_key = SecretKey::parse(&priv_key)
        .map_err(|e| to_custom_err(format!("Error parsing SecretKey {e}")))?;

    match message {
        TransactionMessage::Legacy(m) => {
            let signing_message = libsecp256k1::Message::parse_slice(&m.hash()[..])
                .map_err(|_| to_custom_err("Invalid signing message"))?;
            let (signature, recid) = libsecp256k1::sign(&signing_message, &secret_key);
            let v = match m.chain_id {
                None => 27 + u64::from(recid.serialize()),
                Some(chain_id) => 2 * chain_id + 35 + u64::from(recid.serialize()),
            };
            let rs = signature.serialize();
            let r = H256::from_slice(&rs[0..32]);
            let s = H256::from_slice(&rs[32..64]);

            Ok(TransactionV2::Legacy(ethereum::LegacyTransaction {
                nonce: m.nonce,
                value: m.value,
                input: m.input,
                signature: ethereum::TransactionSignature::new(v, r, s)
                    .ok_or(to_custom_err("Signer generated invalid signature"))?,
                gas_price: m.gas_price,
                gas_limit: m.gas_limit,
                action: m.action,
            }))
        }
        TransactionMessage::EIP2930(m) => {
            let signing_message = libsecp256k1::Message::parse_slice(&m.hash()[..])
                .map_err(|_| to_custom_err("Invalid signing message"))?;
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
                .map_err(|_| to_custom_err("Invalid signing message"))?;
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
