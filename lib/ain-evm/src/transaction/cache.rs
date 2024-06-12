use std::num::NonZeroUsize;

use ethereum::{EnvelopedEncodable, TransactionV2};
use ethereum_types::U256;
use log::trace;
use lru::LruCache;

use crate::{transaction::SignedTx, Result};

#[derive(Debug, Default)]
pub struct TransactionCache {
    pub signed_tx_cache: SignedTxCache,
    pub tx_validation_cache: TxValidationCache,
}

impl TransactionCache {
    pub fn new() -> Self {
        Self {
            signed_tx_cache: SignedTxCache::default(),
            tx_validation_cache: TxValidationCache::default(),
        }
    }
}

/// Signed transactions cache methods
impl TransactionCache {
    pub fn try_get_or_create(&self, key: &str) -> Result<SignedTx> {
        let mut guard = self.signed_tx_cache.inner.lock();
        trace!("[signed-tx-cache]::get: {}", key);
        let res = guard.try_get_or_insert(key.to_string(), || {
            trace!("[signed-tx-cache]::create {}", key);
            SignedTx::try_from(key)
        })?;
        Ok(res.clone())
    }

    pub fn pre_populate(&self, key: &str, signed_tx: SignedTx) -> Result<()> {
        let mut guard = self.signed_tx_cache.inner.lock();
        trace!("[signed-tx-cache]::pre_populate: {}", key);
        let _ = guard.get_or_insert(key.to_string(), move || {
            trace!("[signed-tx-cache]::pre_populate:: create {}", key);
            signed_tx
        });

        Ok(())
    }

    pub fn try_get_or_create_from_tx(&self, tx: &TransactionV2) -> Result<SignedTx> {
        let data = EnvelopedEncodable::encode(tx);
        let key = hex::encode(&data);
        let mut guard = self.signed_tx_cache.inner.lock();
        trace!("[signed-tx-cache]::get from tx: {}", &key);
        let res = guard.try_get_or_insert(key.clone(), || {
            trace!("[signed-tx-cache]::create from tx {}", &key);
            SignedTx::try_from(key.as_str())
        })?;
        Ok(res.clone())
    }
}

/// Transaction validation cache methods
impl TransactionCache {
    pub fn get_stateless(&self, key: &str) -> Option<ValidateTxInfo> {
        self.tx_validation_cache.stateless.lock().get(key).cloned()
    }

    pub fn set_stateless(&self, key: String, value: ValidateTxInfo) -> ValidateTxInfo {
        let mut cache = self.tx_validation_cache.stateless.lock();
        cache.put(key, value.clone());
        value
    }
}

#[derive(Debug)]
pub struct SignedTxCache {
    inner: spin::Mutex<LruCache<String, SignedTx>>,
}

impl Default for SignedTxCache {
    fn default() -> Self {
        Self::new(ain_cpp_imports::get_ecc_lru_cache_count())
    }
}

impl SignedTxCache {
    pub fn new(capacity: usize) -> Self {
        Self {
            inner: spin::Mutex::new(LruCache::new(NonZeroUsize::new(capacity).unwrap())),
        }
    }
}

#[derive(Clone, Debug)]
pub struct ValidateTxInfo {
    pub signed_tx: SignedTx,
    pub max_prepay_fee: U256,
}

#[derive(Debug)]
pub struct TxValidationCache {
    stateless: spin::Mutex<LruCache<String, ValidateTxInfo>>,
}

impl Default for TxValidationCache {
    fn default() -> Self {
        Self::new(ain_cpp_imports::get_evmv_lru_cache_count())
    }
}

impl TxValidationCache {
    pub fn new(capacity: usize) -> Self {
        Self {
            stateless: spin::Mutex::new(LruCache::new(NonZeroUsize::new(capacity).unwrap())),
        }
    }
}
