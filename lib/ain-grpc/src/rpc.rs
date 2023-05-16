use crate::block::{BlockNumber, RpcBlock};
use crate::bytes::Bytes;
use crate::call_request::CallRequest;
use crate::codegen::types::EthTransactionInfo;

use crate::receipt::ReceiptResult;
use crate::transaction_request::{TransactionMessage, TransactionRequest};
use ain_cpp_imports::get_eth_priv_key;
use ain_evm::executor::TxResponse;
use ain_evm::handler::Handlers;

use ain_evm::storage::traits::{BlockStorage, ReceiptStorage, TransactionStorage};
use ain_evm::transaction::{SignedTx, TransactionError};
use ethereum::TransactionV2 as EthereumTransaction;
use jsonrpsee::core::{Error, RpcResult};
use jsonrpsee::proc_macros::rpc;
use libsecp256k1::SecretKey;
use log::debug;
use primitive_types::{H160, H256, U256};
use std::convert::Into;
use std::str::FromStr;
use std::sync::Arc;

#[rpc(server, client)]
pub trait MetachainRPC {
    // ----------------------------------------
    // Client
    // ----------------------------------------

    /// Makes a call to the Ethereum node without creating a transaction on the blockchain.
    /// Returns the output data as a hexadecimal string.
    #[method(name = "eth_call")]
    fn call(&self, input: CallRequest, block_number: Option<BlockNumber>) -> RpcResult<Bytes>;

    /// Retrieves the list of accounts managed by the node.
    /// Returns a vector of Ethereum addresses as hexadecimal strings.
    #[method(name = "eth_accounts")]
    fn accounts(&self) -> RpcResult<Vec<String>>;

    /// Returns the current chain ID as a hexadecimal string.
    #[method(name = "eth_chainId")]
    fn chain_id(&self) -> RpcResult<String>;

    /// Returns the current network ID as a string.
    #[method(name = "net_version")]
    fn net_version(&self) -> RpcResult<String>;

    // ----------------------------------------
    // Block
    // ----------------------------------------

    /// Returns the current block number as U256.
    #[method(name = "eth_blockNumber")]
    fn block_number(&self) -> RpcResult<U256>;

    /// Retrieves a specific block, identified by its block number.
    /// Returns full transaction info or transaction hash depending on full_transactions param
    #[method(name = "eth_getBlockByNumber")]
    fn get_block_by_number(
        &self,
        block_number: BlockNumber,
        full_transactions: Option<bool>,
    ) -> RpcResult<Option<RpcBlock>>;

    /// Retrieves a specific block, identified by its hash.
    #[method(name = "eth_getBlockByHash")]
    fn get_block_by_hash(
        &self,
        hash: H256,
        full_transactions: Option<bool>,
    ) -> RpcResult<Option<RpcBlock>>;

    /// Retrieves the transaction count for a specific block, identified by its hash.
    #[method(name = "eth_getBlockTransactionCountByHash")]
    fn get_block_transaction_count_by_hash(&self, hash: H256) -> RpcResult<usize>;

    /// Retrieves the transaction count for a specific block, identified by its block number.
    #[method(name = "eth_getBlockTransactionCountByNumber")]
    fn get_block_transaction_count_by_number(&self, number: BlockNumber) -> RpcResult<usize>;

    // ----------------------------------------
    // Mining
    // ----------------------------------------

    /// Checks if the node is currently mining.
    #[method(name = "eth_mining")]
    fn mining(&self) -> RpcResult<bool>;

    /// Returns the hash of the current block, the seedHash, and the boundary condition to be met ("target").
    #[method(name = "eth_getWork")]
    fn get_getwork(&self) -> RpcResult<Vec<String>>;

    /// Submits a proof of work solution to the node.
    /// Always returns false
    #[method(name = "eth_submitWork")]
    fn eth_submitwork(&self, nonce: String, hash: String, digest: String) -> RpcResult<bool>;

    /// Retrieves the current hash rate of the node.
    /// Always returns 0x0
    #[method(name = "eth_hashrate")]
    fn hash_rate(&self) -> RpcResult<String>;

    /// Submit mining hashrate.
    /// Always returns false
    #[method(name = "eth_submitHashrate")]
    fn eth_submithashrate(&self, hashrate: String, id: String) -> RpcResult<bool>;

    // ----------------------------------------
    // Transaction
    // ----------------------------------------

    /// Retrieves a specific transaction, identified by its hash.
    #[method(name = "eth_getTransactionByHash")]
    fn get_transaction_by_hash(&self, hash: H256) -> RpcResult<Option<EthTransactionInfo>>;

    /// Retrieves a specific transaction, identified by the block hash and transaction index.
    #[method(name = "eth_getTransactionByBlockHashAndIndex")]
    fn get_transaction_by_block_hash_and_index(
        &self,
        hash: H256,
        index: usize,
    ) -> RpcResult<Option<EthTransactionInfo>>;

    /// Retrieves a specific transaction, identified by the block number and transaction index.
    #[method(name = "eth_getTransactionByBlockNumberAndIndex")]
    fn get_transaction_by_block_number_and_index(
        &self,
        block_number: U256,
        index: usize,
    ) -> RpcResult<Option<EthTransactionInfo>>;

    /// Retrieves the list of pending transactions.
    #[method(name = "eth_pendingTransactions")]
    fn get_pending_transaction(&self) -> RpcResult<Vec<EthTransactionInfo>>;

    /// Retrieves the receipt of a specific transaction, identified by its hash.
    #[method(name = "eth_getTransactionReceipt")]
    fn get_receipt(&self, hash: H256) -> RpcResult<Option<ReceiptResult>>;

    /// Retrieves the number of transactions sent from a specific address.
    #[method(name = "eth_getTransactionCount")]
    fn get_transaction_count(
        &self,
        address: H160,
        block_number: Option<BlockNumber>,
    ) -> RpcResult<U256>;

    // ----------------------------------------
    // State
    // ----------------------------------------

    /// Retrieves the balance of a specific Ethereum address at a given block number.
    /// Returns the balance as U256.
    #[method(name = "eth_getBalance")]
    fn get_balance(&self, address: H160, block_number: Option<BlockNumber>) -> RpcResult<U256>;

    /// Retrieves the bytecode of a contract at a specific address.
    #[method(name = "eth_getCode")]
    fn get_code(&self, address: H160, block_number: Option<BlockNumber>) -> RpcResult<String>;

    /// Retrieves the storage value at a specific position in a contract.
    #[method(name = "eth_getStorageAt")]
    fn get_storage_at(
        &self,
        address: H160,
        position: U256,
        block_number: Option<BlockNumber>,
    ) -> RpcResult<H256>;

    // ----------------------------------------
    // Send
    // ----------------------------------------

    /// Sends a signed transaction.
    /// Returns the transaction hash as a hexadecimal string.
    #[method(name = "eth_sendRawTransaction")]
    fn send_raw_transaction(&self, tx: &str) -> RpcResult<String>;

    /// Sends a transaction.
    /// Returns the transaction hash as a hexadecimal string.
    #[method(name = "eth_sendTransaction")]
    fn send_transaction(&self, req: TransactionRequest) -> RpcResult<String>;

    // ----------------------------------------
    // Gas
    // ----------------------------------------

    /// Estimate gas needed for execution of given contract.
    #[method(name = "eth_estimateGas")]
    fn estimate_gas(&self, input: CallRequest) -> RpcResult<U256>;

    /// Returns current gas_price.
    #[method(name = "eth_gasPrice")]
    fn gas_price(&self) -> RpcResult<U256>;

    // Dump full db
    #[method(name = "dumpdb")]
    fn dump_db(&self) -> RpcResult<()>;
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
    fn call(&self, input: CallRequest, _block_number: Option<BlockNumber>) -> RpcResult<Bytes> {
        debug!(target:"rpc", "[RPC] Call input {:#?}", input);
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
                &data.map(|d| d.0).unwrap_or_default(),
                gas.unwrap_or_default().as_u64(),
                vec![],
            )
            .map_err(|e| Error::Custom(format!("Error calling EVM : {e:?}")))?;

        Ok(Bytes(data))
    }

    fn accounts(&self) -> RpcResult<Vec<String>> {
        let accounts = ain_cpp_imports::get_accounts().unwrap();
        Ok(accounts)
    }

    // State RPC

    fn get_balance(&self, address: H160, block_number: Option<BlockNumber>) -> RpcResult<U256> {
        let block_number = self.block_number_to_u256(block_number);
        debug!(target:"rpc",
            "Getting balance for address: {:?} at block : {} ",
            address, block_number
        );
        let balance = self
            .handler
            .evm
            .get_balance(address, block_number)
            .unwrap_or(U256::zero());

        debug!(target:"rpc","Address: {:?} balance : {} ", address, balance);
        Ok(balance)
    }

    fn get_code(&self, address: H160, block_number: Option<BlockNumber>) -> RpcResult<String> {
        let block_number = self.block_number_to_u256(block_number);

        debug!(target:"rpc",
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
        debug!(target:"rpc",
            "Getting storage for address: {:?}, at position {:?}, for block {}",
            address, position, block_number
        );

        self.handler
            .evm
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

        debug!(target:"rpc","Current block number: {:?}", count);
        Ok(count)
    }

    fn get_block_by_number(
        &self,
        block_number: BlockNumber,
        full_transactions: Option<bool>,
    ) -> RpcResult<Option<RpcBlock>> {
        let block_number = self.block_number_to_u256(Some(block_number));
        debug!(target:"rpc","Getting block by number : {}", block_number);
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
                let transaction_info = tx
                    .try_into()
                    .map_err(|e: TransactionError| Error::Custom(e.to_string()))?;
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
        let block_number = self.block_number_to_u256(Some(block_number));
        self.handler
            .storage
            .get_block_by_number(&block_number)
            .map_or(Ok(0), |b| Ok(b.transactions.len()))
    }

    fn send_transaction(&self, request: TransactionRequest) -> RpcResult<String> {
        debug!(target:"rpc","[send_transaction] Sending transaction: {:?}", request);

        let from = match request.from {
            Some(from) => from,
            None => {
                let accounts = match self.accounts() {
                    Ok(accounts) => accounts,
                    Err(e) => return Err(e),
                };

                match accounts.get(0) {
                    Some(account) => H160::from_str(account.as_str()).unwrap(),
                    None => return Err(Error::Custom(String::from("from is not available"))),
                }
            }
        };

        let chain_id = ain_cpp_imports::get_chain_id()
            .map_err(|e| Error::Custom(format!("ain_cpp_imports::get_chain_id error : {e:?}")))?;

        let block_number = BlockNumber::Num(self.block_number().unwrap().as_u64());

        let nonce = match request.nonce {
            Some(nonce) => nonce,
            None => match self.get_transaction_count(from, Some(block_number)) {
                Ok(nonce) => nonce,
                Err(e) => return Err(e),
            },
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
                m.gas_limit = U256::from(1);
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
                )))
            }
        };

        let transaction = sign(from, message)?;
        let hash = self.send_raw_transaction(format!("{:#x}", transaction.hash()).as_str())?;
        Ok(hash)
    }

    fn send_raw_transaction(&self, tx: &str) -> RpcResult<String> {
        debug!(target:"rpc","[send_raw_transaction] Sending raw transaction: {:?}", tx);
        let raw_tx = tx.strip_prefix("0x").unwrap_or(tx);
        let hex =
            hex::decode(raw_tx).map_err(|e| Error::Custom(format!("Eror decoding TX {e:?}")))?;

        match ain_cpp_imports::publish_eth_transaction(hex) {
            Ok(true) => {
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
            }
            Ok(false) => {
                debug!(target:"rpc","[send_raw_transaction] Could not publish raw transaction: {tx}");
                Err(Error::Custom(format!(
                    "Could not publish raw transaction: {tx}"
                )))
            }
            Err(e) => {
                debug!(target:"rpc","[send_raw_transaction] Error publishing TX {e:?}");
                Err(Error::Custom(format!("Error publishing TX {e:?}")))
            }
        }
    }

    fn get_transaction_count(
        &self,
        address: H160,
        block_number: Option<BlockNumber>,
    ) -> RpcResult<U256> {
        debug!(target:"rpc","Getting transaction count for address: {:?}", address);
        let block_number = self.block_number_to_u256(block_number);
        let nonce = self
            .handler
            .evm
            .get_nonce(address, block_number)
            .map_err(|e| {
                Error::Custom(format!("Error getting address transaction count : {e:?}"))
            })?;

        debug!(target:"rpc","Count: {:#?}", nonce);
        Ok(nonce)
    }

    fn estimate_gas(&self, input: CallRequest) -> RpcResult<U256> {
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
                &data.map(|d| d.0).unwrap_or_default(),
                gas.unwrap_or_default().as_u64(),
                vec![],
            )
            .map_err(|e| Error::Custom(format!("Error calling EVM : {e:?}")))?;

        let native_size = ain_cpp_imports::get_native_tx_size(data).unwrap_or(0);
        debug!(target:"rpc","estimateGas: {:#?} + {:#?}", native_size, used_gas);
        Ok(U256::from(native_size + std::cmp::max(21000, used_gas)))
    }

    fn gas_price(&self) -> RpcResult<U256> {
        let gas_price = ain_cpp_imports::get_min_relay_tx_fee().unwrap_or(10);
        debug!(target:"rpc","gasPrice: {:#?}", gas_price);
        Ok(U256::from(gas_price))
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

    fn dump_db(&self) -> RpcResult<()> {
        self.handler.storage.dump_db();
        Ok(())
    }
}

fn sign(address: H160, message: TransactionMessage) -> RpcResult<EthereumTransaction> {
    let mut key_id: [u8; 20] = [0; 20];
    hex::decode_to_slice(address, &mut key_id)
        .map_err(|e| Error::Custom(format!("Error decoding H160 address {e:?}")))?;
    let priv_key = get_eth_priv_key(key_id).unwrap();
    let secret_key = SecretKey::parse(&priv_key).unwrap();

    let mut transaction = None;

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
            transaction = Some(EthereumTransaction::Legacy(ethereum::LegacyTransaction {
                nonce: m.nonce,
                gas_price: m.gas_price,
                gas_limit: m.gas_limit,
                action: m.action,
                value: m.value,
                input: m.input,
                signature: ethereum::TransactionSignature::new(v, r, s).ok_or_else(|| {
                    Error::Custom(String::from("signer generated invalid signature"))
                })?,
            }));
        }
        TransactionMessage::EIP2930(m) => {
            let signing_message = libsecp256k1::Message::parse_slice(&m.hash()[..])
                .map_err(|_| Error::Custom(String::from("invalid signing message")))?;
            let (signature, recid) = libsecp256k1::sign(&signing_message, &secret_key);
            let rs = signature.serialize();
            let r = H256::from_slice(&rs[0..32]);
            let s = H256::from_slice(&rs[32..64]);
            transaction = Some(EthereumTransaction::EIP2930(ethereum::EIP2930Transaction {
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
            }));
        }
        TransactionMessage::EIP1559(m) => {
            let signing_message = libsecp256k1::Message::parse_slice(&m.hash()[..])
                .map_err(|_| Error::Custom(String::from("invalid signing message")))?;
            let (signature, recid) = libsecp256k1::sign(&signing_message, &secret_key);
            let rs = signature.serialize();
            let r = H256::from_slice(&rs[0..32]);
            let s = H256::from_slice(&rs[32..64]);
            transaction = Some(EthereumTransaction::EIP1559(ethereum::EIP1559Transaction {
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
            }));
        }
    }

    Ok(transaction.unwrap())
}
