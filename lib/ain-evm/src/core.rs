use std::{
    collections::{BTreeSet, HashMap},
    num::NonZeroUsize,
    path::PathBuf,
    sync::Arc,
};

use ain_contracts::{
    dst20_address_from_token_id, get_transfer_domain_contract,
    get_transferdomain_dst20_transfer_function, get_transferdomain_native_transfer_function,
    FixedContract,
};
use anyhow::format_err;
use ethereum::{
    AccessList, Account, Block, EnvelopedEncodable, Log, PartialHeader, TransactionAction,
    TransactionV2,
};
use ethereum_types::{Bloom, BloomInput, H160, H256, U256};
use log::{debug, trace};
use lru::LruCache;
use parking_lot::Mutex;
use vsdb_core::vsdb_set_base_dir;

use crate::{
    backend::{BackendError, EVMBackend, Vicinity},
    block::INITIAL_BASE_FEE,
    blocktemplate::BlockTemplateMap,
    executor::{AinExecutor, ExecutorContext, TxResponse},
    fee::calculate_max_prepay_gas_fee,
    gas::check_tx_intrinsic_gas,
    receipt::ReceiptService,
    storage::{traits::BlockStorage, Storage},
    transaction::SignedTx,
    trie::TrieDBStore,
    weiamount::{try_from_satoshi, WeiAmount},
    Result,
};

pub type XHash = String;

pub struct SignedTxCache {
    inner: spin::Mutex<LruCache<String, SignedTx>>,
}

const DEFAULT_CACHE_SIZE: usize = 10000;

impl Default for SignedTxCache {
    fn default() -> Self {
        Self::new(DEFAULT_CACHE_SIZE)
    }
}

impl SignedTxCache {
    pub fn new(capacity: usize) -> Self {
        Self {
            inner: spin::Mutex::new(LruCache::new(NonZeroUsize::new(capacity).unwrap())),
        }
    }

    pub fn try_get_or_create(&self, key: &str) -> Result<SignedTx> {
        let mut guard = self.inner.lock();
        debug!("[signed-tx-cache]::get: {}", key);
        let res = guard.try_get_or_insert(key.to_string(), || {
            debug!("[signed-tx-cache]::create {}", key);
            SignedTx::try_from(key)
        })?;
        Ok(res.clone())
    }

    pub fn try_get_or_create_from_tx(&self, tx: &TransactionV2) -> Result<SignedTx> {
        let data = EnvelopedEncodable::encode(tx);
        let key = hex::encode(&data);
        let mut guard = self.inner.lock();
        debug!("[signed-tx-cache]::get from tx: {}", &key);
        let res = guard.try_get_or_insert(key.clone(), || {
            debug!("[signed-tx-cache]::create from tx {}", &key);
            SignedTx::try_from(key.as_str())
        })?;
        Ok(res.clone())
    }
}

struct TxValidationCache {
    validated: spin::Mutex<LruCache<(H256, String), ValidateTxInfo>>,
    stateless: spin::Mutex<LruCache<String, ValidateTxInfo>>,
}

impl Default for TxValidationCache {
    fn default() -> Self {
        Self::new(DEFAULT_CACHE_SIZE)
    }
}

impl TxValidationCache {
    pub fn new(capacity: usize) -> Self {
        Self {
            validated: spin::Mutex::new(LruCache::new(NonZeroUsize::new(capacity).unwrap())),
            stateless: spin::Mutex::new(LruCache::new(NonZeroUsize::new(capacity).unwrap())),
        }
    }

    pub fn get(&self, key: &(H256, String)) -> Option<ValidateTxInfo> {
        self.validated.lock().get(key).cloned()
    }

    pub fn get_stateless(&self, key: &str) -> Option<ValidateTxInfo> {
        self.stateless.lock().get(key).cloned()
    }

    pub fn set(&self, key: (H256, String), value: ValidateTxInfo) -> ValidateTxInfo {
        let mut cache = self.validated.lock();
        cache.put(key, value.clone());
        value
    }

    pub fn set_stateless(&self, key: String, value: ValidateTxInfo) -> ValidateTxInfo {
        let mut cache = self.stateless.lock();
        cache.put(key, value.clone());
        value
    }

    // To be used on new block or any known state changes. Only clears fully validated TX cache.
    // Stateless cache can be kept across blocks and is handled by LRU itself
    pub fn clear(&self) {
        let mut cache = self.validated.lock();
        cache.clear()
    }
}

pub struct EVMCoreService {
    pub block_templates: Arc<BlockTemplateMap>,
    pub trie_store: Arc<TrieDBStore>,
    pub signed_tx_cache: SignedTxCache,
    storage: Arc<Storage>,
    nonce_store: Mutex<HashMap<H160, BTreeSet<U256>>>,
    tx_validation_cache: TxValidationCache,
}
pub struct EthCallArgs<'a> {
    pub caller: Option<H160>,
    pub to: Option<H160>,
    pub value: U256,
    pub data: &'a [u8],
    pub gas_limit: u64,
    pub gas_price: Option<U256>,
    pub max_fee_per_gas: Option<U256>,
    pub access_list: AccessList,
    pub block_number: U256,
    pub transaction_type: Option<U256>,
}

#[derive(Clone, Debug)]
pub struct ValidateTxInfo {
    pub signed_tx: SignedTx,
    pub max_prepay_fee: U256,
}

pub struct TransferDomainTxInfo {
    pub from: String,
    pub to: String,
    pub native_address: String,
    pub direction: bool,
    pub value: u64,
    pub token_id: u32,
}

fn init_vsdb(path: PathBuf) {
    debug!(target: "vsdb", "Initializating VSDB");
    let vsdb_dir_path = path.join(".vsdb");
    vsdb_set_base_dir(&vsdb_dir_path).expect("Could not update vsdb base dir");
    debug!(target: "vsdb", "VSDB directory : {}", vsdb_dir_path.display());
}

impl EVMCoreService {
    pub fn restore(storage: Arc<Storage>, path: PathBuf) -> Self {
        init_vsdb(path);

        Self {
            block_templates: Arc::new(BlockTemplateMap::new()),
            trie_store: Arc::new(TrieDBStore::restore()),
            signed_tx_cache: SignedTxCache::default(),
            storage,
            nonce_store: Mutex::new(HashMap::new()),
            tx_validation_cache: TxValidationCache::default(),
        }
    }

    pub fn new_from_json(
        storage: Arc<Storage>,
        genesis_path: PathBuf,
        evm_datadir: PathBuf,
    ) -> Result<Self> {
        debug!("Loading genesis state from {}", genesis_path.display());
        init_vsdb(evm_datadir);

        let handler = Self {
            block_templates: Arc::new(BlockTemplateMap::new()),
            trie_store: Arc::new(TrieDBStore::new()),
            signed_tx_cache: SignedTxCache::default(),
            storage: Arc::clone(&storage),
            nonce_store: Mutex::new(HashMap::new()),
            tx_validation_cache: TxValidationCache::default(),
        };
        let (state_root, genesis) = TrieDBStore::genesis_state_root_from_json(
            &handler.trie_store,
            &handler.storage,
            genesis_path,
        )?;

        let gas_limit = storage.get_attributes_or_default()?.block_gas_limit;
        let block: Block<TransactionV2> = Block::new(
            PartialHeader {
                state_root,
                number: U256::zero(),
                beneficiary: H160::default(),
                receipts_root: ReceiptService::get_receipts_root(&Vec::new()),
                logs_bloom: Bloom::default(),
                gas_used: U256::default(),
                gas_limit: genesis.gas_limit.unwrap_or(U256::from(gas_limit)),
                extra_data: genesis.extra_data.unwrap_or_default().into(),
                parent_hash: genesis.parent_hash.unwrap_or_default(),
                mix_hash: genesis.mix_hash.unwrap_or_default(),
                nonce: genesis.nonce.unwrap_or_default(),
                timestamp: genesis.timestamp.unwrap_or_default().as_u64(),
                difficulty: genesis.difficulty.unwrap_or_default(),
                base_fee: genesis.base_fee.unwrap_or(INITIAL_BASE_FEE),
            },
            Vec::new(),
            Vec::new(),
        );
        storage.put_latest_block(Some(&block))?;
        storage.put_block(&block)?;

        Ok(handler)
    }

    pub fn flush(&self) -> Result<()> {
        self.trie_store.flush()
    }

    pub fn call(&self, arguments: EthCallArgs) -> Result<TxResponse> {
        let EthCallArgs {
            caller,
            to,
            value,
            data,
            gas_limit,
            gas_price,
            max_fee_per_gas,
            access_list,
            block_number,
            transaction_type,
        } = arguments;

        let (state_root, block_number, beneficiary, base_fee, timestamp) = self
            .storage
            .get_block_by_number(&block_number)?
            .map(|block| {
                (
                    block.header.state_root,
                    block.header.number,
                    block.header.beneficiary,
                    block.header.base_fee,
                    block.header.timestamp,
                )
            })
            .unwrap_or_default();
        debug!(
            "Calling EVM at block number : {:#x}, state_root : {:#x}",
            block_number, state_root
        );
        debug!("[call] caller: {:?}", caller);
        let vicinity = Vicinity {
            block_number,
            origin: caller.unwrap_or_default(),
            gas_limit: U256::from(gas_limit),
            total_gas_used: U256::zero(),
            block_gas_limit: U256::from(self.storage.get_attributes_or_default()?.block_gas_limit),
            gas_price: if transaction_type == Some(U256::from(2)) {
                max_fee_per_gas.unwrap_or_default()
            } else {
                gas_price.unwrap_or_default()
            },
            beneficiary,
            block_base_fee_per_gas: base_fee,
            timestamp: U256::from(timestamp),
            ..Vicinity::default()
        };
        debug!("[call] vicinity: {:?}", vicinity);

        let mut backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            vicinity,
        )
        .map_err(|e| format_err!("------ Could not restore backend {}", e))?;

        Ok(AinExecutor::new(&mut backend).call(ExecutorContext {
            caller: caller.unwrap_or_default(),
            to,
            value,
            data,
            gas_limit,
            access_list,
        }))
    }

    /// Validates a raw tx.
    ///
    /// The validation checks of the tx before we consider it to be valid are:
    /// 1. Gas price check: verify that the maximum gas price is minimally of the block initial base fee.
    /// 2. Gas price and tx value check: verify that amount is within money range.
    /// 3. Intrinsic gas limit check: verify that the tx intrinsic gas is within the tx gas limit.
    /// 4. Gas limit check: verify that the tx gas limit is not higher than the maximum gas per block.
    /// 5. Account balance check: verify that the account balance must minimally have the tx prepay gas fee.
    /// 6. Account nonce check: verify that the tx nonce must be more than or equal to the account nonce.
    ///
    /// # Arguments
    ///
    /// * `tx` - The raw tx.
    /// * `template_id` - The template_id template number.
    ///
    /// # Returns
    ///
    /// Returns the signed tx, tx prepay gas fees and the gas used to call the tx.
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn validate_raw_tx(&self, tx: &str, template_id: u64) -> Result<ValidateTxInfo> {
        let state_root = self.block_templates.get_latest_state_root_in(template_id)?;
        debug!("[validate_raw_tx] state_root : {:#?}", state_root);

        if let Some(tx_info) = self
            .tx_validation_cache
            .get(&(state_root, String::from(tx)))
        {
            return Ok(tx_info);
        }

        debug!("[validate_raw_tx] template_id {}", template_id);
        debug!("[validate_raw_tx] raw transaction : {:#?}", tx);

        let ValidateTxInfo {
            signed_tx,
            max_prepay_fee,
        } = if let Some(validate_info) = self.tx_validation_cache.get_stateless(tx) {
            validate_info
        } else {
            let signed_tx = self
                .signed_tx_cache
                .try_get_or_create(tx)
                .map_err(|_| format_err!("Error: decoding raw tx to TransactionV2"))?;
            debug!("[validate_raw_tx] signed_tx : {:#?}", signed_tx);

            debug!(
                "[validate_raw_tx] signed_tx.sender : {:#?}",
                signed_tx.sender
            );

            // Validate tx gas price with initial block base fee
            let tx_gas_price = signed_tx.gas_price();
            if tx_gas_price < INITIAL_BASE_FEE {
                debug!("[validate_raw_tx] tx gas price is lower than initial block base fee");
                return Err(
                    format_err!("tx gas price is lower than initial block base fee").into(),
                );
            }

            // Validate tx gas price and tx value within money range
            if !WeiAmount(tx_gas_price).wei_range() || !WeiAmount(signed_tx.value()).wei_range() {
                debug!("[validate_raw_tx] value more than money range");
                return Err(format_err!("value more than money range").into());
            }

            // Validate tx gas limit with intrinsic gas
            check_tx_intrinsic_gas(&signed_tx)?;

            // Validate gas limit
            let gas_limit = signed_tx.gas_limit();
            let block_gas_limit = self.storage.get_attributes_or_default()?.block_gas_limit;
            if gas_limit > U256::from(block_gas_limit) {
                debug!("[validate_raw_tx] gas limit higher than max_gas_per_block");
                return Err(format_err!("gas limit higher than max_gas_per_block").into());
            }

            let max_prepay_fee = calculate_max_prepay_gas_fee(&signed_tx)?;
            debug!("[validate_raw_tx] max_prepay_fee : {:x?}", max_prepay_fee);

            self.tx_validation_cache.set_stateless(
                String::from(tx),
                ValidateTxInfo {
                    signed_tx,
                    max_prepay_fee,
                },
            )
        };

        // Start of stateful checks
        // Validate tx prepay fees with account balance
        let backend = self.get_backend(state_root)?;
        let balance = backend.get_balance(&signed_tx.sender);
        debug!("[validate_raw_tx] Account balance : {:x?}", balance);
        if balance < max_prepay_fee {
            debug!("[validate_raw_tx] insufficient balance to pay fees");
            return Err(format_err!("insufficient balance to pay fees").into());
        }

        let nonce = backend.get_nonce(&signed_tx.sender);
        debug!(
            "[validate_raw_tx] signed_tx nonce : {:#?}",
            signed_tx.nonce()
        );
        debug!("[validate_raw_tx] nonce : {:#?}", nonce);
        // Validate tx nonce with account nonce
        if nonce > signed_tx.nonce() {
            return Err(format_err!(
                "invalid nonce. Account nonce {}, signed_tx nonce {}",
                nonce,
                signed_tx.nonce()
            )
            .into());
        }

        Ok(self.tx_validation_cache.set(
            (state_root, String::from(tx)),
            ValidateTxInfo {
                signed_tx,
                max_prepay_fee,
            },
        ))
    }

    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get_total_gas_used(&self, template_id: u64) -> String {
        let res = self
            .block_templates
            .get_total_gas_used_in(template_id)
            .unwrap_or_default();
        format!("{:064x}", res)
    }

    /// Validates a raw transfer domain tx.
    ///
    /// The validation checks of the tx before we consider it to be valid are:
    /// 1. Verify that transferdomain tx is of legacy transaction type.
    /// 2. Verify that tx sender matches transferdomain from address.
    /// 3. Verify that tx value is set to zero.
    /// 4. Verify that gas price is set to zero.
    /// 5. Verify that gas limit is set to zero.
    /// 6. Verify that transaction action is a call to the transferdomain contract address.
    ///
    /// 7. If native token transfer:
    /// - Verify that tx function signature is transfer.
    /// - Verify that from address specified matches transferdomain tx information
    /// - Verify that to address specified matches transferdomain tx information
    /// - Verify that value specified matches transferdomain tx information
    /// - Verify that native address specified matches transferdomain tx information
    ///
    /// 8. If DST20 token transfer:
    /// - Verify that tx function signature is transferDST20.
    /// - Verify that contract address specified matches DST20 token ID address.
    /// - Verify that from address specified matches transferdomain tx information
    /// - Verify that to address specified matches transferdomain tx information
    /// - Verify that value specified matches transferdomain tx information
    /// - Verify that native address specified matches transferdomain tx information
    ///
    /// 9. Account nonce check: verify that the tx nonce must be more than or equal to the account nonce.
    ///
    /// # Arguments
    ///
    /// * `tx` - The raw tx.
    /// * `template_id` - The template_id template number.
    ///
    /// # Returns
    ///
    /// Returns the validation result.
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn validate_raw_transferdomain_tx(
        &self,
        tx: &str,
        template_id: u64,
        context: TransferDomainTxInfo,
    ) -> Result<ValidateTxInfo> {
        debug!(
            "[validate_raw_transferdomain_tx] template_id {}",
            template_id
        );
        debug!(
            "[validate_raw_transferdomain_tx] raw transaction : {:#?}",
            tx
        );

        let state_root = self.block_templates.get_latest_state_root_in(template_id)?;
        debug!(
            "[validate_raw_transferdomain_tx] state_root : {:#?}",
            state_root
        );

        let ValidateTxInfo {
            signed_tx,
            max_prepay_fee,
        } = if let Some(validate_info) = self.tx_validation_cache.get_stateless(tx) {
            validate_info
        } else {
            let signed_tx = self
                .signed_tx_cache
                .try_get_or_create(tx)
                .map_err(|_| format_err!("Error: decoding raw tx to TransactionV2"))?;
            debug!(
                "[validate_raw_transferdomain_tx] signed_tx : {:#?}",
                signed_tx
            );

            // Validate tx is legacy tx
            if signed_tx.get_tx_type() != U256::zero() {
                return Err(format_err!(
                    "[validate_raw_transferdomain_tx] invalid evm tx type {:#?}",
                    signed_tx.get_tx_type()
                )
                .into());
            }

            // Validate tx sender with transferdomain sender
            let sender = context
                .from
                .parse::<H160>()
                .map_err(|_| "Invalid address")?;
            if signed_tx.sender != sender {
                return Err(format_err!(
                    "[validate_raw_transferdomain_tx] invalid sender, signed_tx.sender : {:#?}, transferdomain sender : {:#?}",
                    signed_tx.sender,
                    sender
                ).into());
            }

            // Validate tx value equal to zero
            if signed_tx.value() != U256::zero() {
                debug!("[validate_raw_transferdomain_tx] value not equal to zero");
                return Err(format_err!("value not equal to zero").into());
            }

            // Validate tx gas price equal to zero
            if signed_tx.gas_price() != U256::zero() {
                debug!("[validate_raw_transferdomain_tx] gas price not equal to zero");
                return Err(format_err!("gas price not equal to zero").into());
            }

            // Validate tx gas limit equal to zero
            if signed_tx.gas_limit() != U256::zero() {
                debug!("[validate_raw_transferdomain_tx] gas limit not equal to zero");
                return Err(format_err!("gas limit not equal to zero").into());
            }

            // Verify transaction action and transferdomain contract address
            let FixedContract { fixed_address, .. } = get_transfer_domain_contract();
            match signed_tx.action() {
                TransactionAction::Call(address) => {
                    if address != fixed_address {
                        return Err(format_err!(
                            "invalid call address. Fixed address: {:#?}, signed_tx call address: {:#?}",
                            fixed_address,
                            address
                    )
                        .into());
                    }
                }
                _ => {
                    return Err(format_err!(
                        "tx action not a call to transferdomain contract address"
                    )
                    .into())
                }
            }

            let (from_address, to_address) = if context.direction {
                // EvmIn
                let to_address = context
                    .to
                    .parse::<H160>()
                    .map_err(|_| "failed to parse to address")?;
                (fixed_address, to_address)
            } else {
                // EvmOut
                let from_address = context
                    .from
                    .parse::<H160>()
                    .map_err(|_| "failed to parse from address")?;
                (from_address, fixed_address)
            };
            let value = try_from_satoshi(U256::from(context.value))?.0;

            let is_native_token_transfer = context.token_id == 0;
            if is_native_token_transfer {
                // Validate function signature
                let function = get_transferdomain_native_transfer_function();
                let function_signature = function.short_signature();
                if function_signature != signed_tx.data()[..4] {
                    return Err(format_err!("invalid function signature input in evm tx").into());
                }

                let token_inputs = function.decode_input(&signed_tx.data()[4..])?;

                // Validate from address input
                let ethabi::Token::Address(input_from_address) = token_inputs[0] else {
                    return Err(format_err!("invalid from address input in evm tx").into());
                };
                if input_from_address != from_address {
                    return Err(format_err!("invalid from address input in evm tx").into());
                }

                // Validate to address input
                let ethabi::Token::Address(input_to_address) = token_inputs[1] else {
                    return Err(format_err!("invalid to address input in evm tx").into());
                };
                if input_to_address != to_address {
                    return Err(format_err!("invalid to address input in evm tx").into());
                }

                // Validate value input
                let ethabi::Token::Uint(input_value) = token_inputs[2] else {
                    return Err(format_err!("invalid value input in evm tx").into());
                };
                if input_value != value {
                    return Err(format_err!("invalid value input in evm tx").into());
                }

                // Validate native address input
                let ethabi::Token::String(ref input_native_address) = token_inputs[3] else {
                    return Err(format_err!("invalid native address input in evm tx").into());
                };
                if context.native_address != *input_native_address {
                    return Err(format_err!("invalid native address input in evm tx").into());
                }
            } else {
                let contract_address = {
                    let address = dst20_address_from_token_id(u64::from(context.token_id))?;
                    ethabi::Token::Address(address)
                };

                // Validate function signature
                let function = get_transferdomain_dst20_transfer_function();
                let function_signature = function.short_signature();
                if function_signature != signed_tx.data()[..4] {
                    return Err(format_err!("invalid function signature input in evm tx").into());
                }

                let token_inputs = function.decode_input(&signed_tx.data()[4..])?;

                // Validate contract address input
                if token_inputs[0] != contract_address {
                    return Err(format_err!("invalid contract address input in evm tx").into());
                }

                // Validate from address input
                let ethabi::Token::Address(input_from_address) = token_inputs[1] else {
                    return Err(format_err!("invalid from address input in evm tx").into());
                };
                if input_from_address != from_address {
                    return Err(format_err!("invalid from address input in evm tx").into());
                }

                // Validate to address input
                let ethabi::Token::Address(input_to_address) = token_inputs[2] else {
                    return Err(format_err!("invalid to address input in evm tx").into());
                };
                if input_to_address != to_address {
                    return Err(format_err!("invalid to address input in evm tx").into());
                }

                // Validate value input
                let ethabi::Token::Uint(input_value) = token_inputs[3] else {
                    return Err(format_err!("invalid value input in evm tx").into());
                };
                if input_value != value {
                    return Err(format_err!("invalid value input in evm tx").into());
                }

                // Validate native address input
                let ethabi::Token::String(ref input_native_address) = token_inputs[4] else {
                    return Err(format_err!("invalid native address input in evm tx").into());
                };
                if context.native_address != *input_native_address {
                    return Err(format_err!("invalid native address input in evm tx").into());
                }
            }

            ValidateTxInfo {
                signed_tx,
                max_prepay_fee: U256::zero(),
            }
        };

        let backend = self.get_backend(state_root)?;

        let nonce = backend.get_nonce(&signed_tx.sender);
        debug!(
            "[validate_raw_transferdomain_tx] signed_tx.sender : {:#?}",
            signed_tx.sender
        );
        debug!(
            "[validate_raw_transferdomain_tx] signed_tx nonce : {:#?}",
            signed_tx.nonce()
        );
        debug!("[validate_raw_transferdomain_tx] nonce : {:#?}", nonce);

        // Validate tx nonce
        if nonce > signed_tx.nonce() {
            return Err(format_err!(
                "Invalid nonce. Account nonce {}, signed_tx nonce {}",
                nonce,
                signed_tx.nonce()
            )
            .into());
        }

        Ok(ValidateTxInfo {
            signed_tx,
            max_prepay_fee,
        })
    }

    pub fn logs_bloom(logs: Vec<Log>, bloom: &mut Bloom) {
        for log in logs {
            bloom.accrue(BloomInput::Raw(&log.address[..]));
            for topic in log.topics {
                bloom.accrue(BloomInput::Raw(&topic[..]));
            }
        }
    }
}

// Block template methods
impl EVMCoreService {
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn remove_block_template(&self, template_id: u64) {
        self.block_templates.remove(template_id);
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn remove_txs_above_hash_in_block_template(
        &self,
        template_id: u64,
        target_hash: XHash,
    ) -> Result<Vec<XHash>> {
        let hashes = self
            .block_templates
            .remove_txs_above_hash_in(template_id, target_hash)?;
        Ok(hashes)
    }

    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless `cs_main` lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get_target_block_in(&self, template_id: u64) -> Result<U256> {
        let target_block = self.block_templates.get_target_block_in(template_id)?;
        Ok(target_block)
    }

    /// Retrieves the next valid nonce for the specified account within a particular block template.
    /// # Arguments
    ///
    /// * `template_id` - The template_id template number.
    /// * `address` - The EVM address of the account whose nonce we want to retrieve.
    ///
    /// # Returns
    ///
    /// Returns the next valid nonce as a `U256`. Defaults to U256::zero()
    ///
    /// # Safety
    ///
    /// Result cannot be used safety unless cs_main lock is taken on C++ side
    /// across all usages. Note: To be replaced with a proper lock flow later.
    ///
    pub unsafe fn get_next_valid_nonce_in_block_template(
        &self,
        template_id: u64,
        address: H160,
    ) -> Result<U256> {
        let state_root = self.block_templates.get_latest_state_root_in(template_id)?;
        let backend = self.get_backend(state_root)?;
        let nonce = backend.get_nonce(&address);
        trace!(
            "[get_next_valid_nonce_in_block_template] Account {address:x?} nonce {nonce:x?} in template_id {template_id}"
        );
        Ok(nonce)
    }
}

// State methods
impl EVMCoreService {
    pub fn get_state_root(&self) -> Result<H256> {
        let state_root = self
            .storage
            .get_latest_block()?
            .map_or(H256::default(), |block| block.header.state_root);
        Ok(state_root)
    }
    pub fn get_account(&self, address: H160, block_number: U256) -> Result<Option<Account>> {
        let state_root = self
            .storage
            .get_block_by_number(&block_number)?
            .map(|block| block.header.state_root)
            .ok_or(format_err!(
                "[get_account] Block number {:x?} not found",
                block_number
            ))?;

        let backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            Vicinity::default(),
        )?;
        Ok(backend.get_account(&address))
    }

    pub fn get_latest_contract_storage(&self, contract: H160, storage_index: H256) -> Result<U256> {
        let state_root = self.get_state_root()?;
        let backend = EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            Vicinity::default(),
        )?;

        backend.get_contract_storage(contract, storage_index.as_bytes())
    }

    pub fn get_code(&self, address: H160, block_number: U256) -> Result<Option<Vec<u8>>> {
        self.get_account(address, block_number)?
            .map_or(Ok(None), |account| {
                self.storage.get_code_by_hash(account.code_hash)
            })
    }

    pub fn get_storage_at(
        &self,
        address: H160,
        position: U256,
        block_number: U256,
    ) -> Result<Option<Vec<u8>>> {
        self.get_account(address, block_number)?
            .map_or(Ok(None), |account| {
                let storage_trie = self
                    .trie_store
                    .trie_db
                    .trie_restore(address.as_bytes(), None, account.storage_root.into())
                    .unwrap();

                let tmp: &mut [u8; 32] = &mut [0; 32];
                position.to_big_endian(tmp);
                storage_trie
                    .get(tmp.as_slice())
                    .map_err(|e| BackendError::TrieError(e.to_string()).into())
            })
    }

    pub fn get_balance(&self, address: H160, block_number: U256) -> Result<U256> {
        let balance = self
            .get_account(address, block_number)?
            .map_or(U256::zero(), |account| account.balance);

        debug!("Account {:x?} balance {:x?}", address, balance);
        Ok(balance)
    }

    pub fn get_nonce_from_block_number(&self, address: H160, block_number: U256) -> Result<U256> {
        let nonce = self
            .get_account(address, block_number)?
            .map_or(U256::zero(), |account| account.nonce);

        debug!("Account {:x?} nonce {:x?}", address, nonce);
        Ok(nonce)
    }

    pub fn get_nonce_from_state_root(&self, address: H160, state_root: H256) -> Result<U256> {
        let backend = self.get_backend(state_root)?;
        let nonce = backend.get_nonce(&address);
        Ok(nonce)
    }

    pub fn get_latest_block_backend(&self) -> Result<EVMBackend> {
        let (state_root, block_number) = self
            .storage
            .get_latest_block()?
            .map(|block| (block.header.state_root, block.header.number))
            .unwrap_or_default();

        trace!(
            "[get_latest_block_backend] At block number : {:#x}, state_root : {:#x}",
            block_number,
            state_root
        );
        EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            Vicinity {
                block_gas_limit: U256::from(
                    self.storage.get_attributes_or_default()?.block_gas_limit,
                ),
                ..Vicinity::default()
            },
        )
    }

    pub fn get_backend(&self, state_root: H256) -> Result<EVMBackend> {
        trace!("[get_backend] state_root : {:#x}", state_root);
        EVMBackend::from_root(
            state_root,
            Arc::clone(&self.trie_store),
            Arc::clone(&self.storage),
            Vicinity {
                block_gas_limit: U256::from(
                    self.storage.get_attributes_or_default()?.block_gas_limit,
                ),
                ..Vicinity::default()
            },
        )
    }

    pub fn get_next_account_nonce(&self, address: H160, state_root: H256) -> Result<U256> {
        let state_root_nonce = self.get_nonce_from_state_root(address, state_root)?;
        let mut nonce_store = self.nonce_store.lock();
        match nonce_store.entry(address) {
            std::collections::hash_map::Entry::Vacant(_) => Ok(state_root_nonce),
            std::collections::hash_map::Entry::Occupied(e) => {
                let nonce_set = e.get();
                if !nonce_set.contains(&state_root_nonce) {
                    return Ok(state_root_nonce);
                }

                let mut nonce = state_root_nonce;
                for elem in nonce_set.range(state_root_nonce..) {
                    if (elem - nonce) > U256::from(1) {
                        break;
                    } else {
                        nonce = *elem;
                    }
                }
                nonce += U256::from(1);
                Ok(nonce)
            }
        }
    }

    pub fn store_account_nonce(&self, address: H160, nonce: U256) -> bool {
        let mut nonce_store = self.nonce_store.lock();
        nonce_store.entry(address).or_default();

        match nonce_store.entry(address) {
            std::collections::hash_map::Entry::Occupied(mut e) => {
                e.get_mut().insert(nonce);
                true
            }
            _ => false,
        }
    }

    pub fn clear_account_nonce(&self) {
        let mut nonce_store = self.nonce_store.lock();
        nonce_store.clear()
    }

    pub fn clear_transaction_cache(&self) {
        self.tx_validation_cache.clear()
    }
}
