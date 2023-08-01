use ethereum_types::{H160, U256};
use rand::Rng;
use std::{
    collections::HashMap,
    sync::{Arc, Mutex, RwLock},
};

use crate::{transaction::system::SystemTx, backend::EVMBackend, executor::AinExecutor};
use crate::{core::NativeTxHash, fee::calculate_gas_fee, transaction::SignedTx};

#[derive(Debug)]
pub struct BlockTemplateMap {
    templates: RwLock<HashMap<u64, BlockTemplate>>,
}

impl Default for BlockTemplateMap {
    fn default() -> Self {
        Self::new()
    }
}

/// Holds multiple `BlockTemplates`s, each associated with a unique template ID.
///
/// Template IDs are randomly generated and used to access distinct block templates.
impl BlockTemplateMap {
    pub fn new() -> Self {
        BlockTemplateMap {
            templates: RwLock::new(HashMap::new()),
        }
    }

    /// `get_template_id` generates a unique random ID, creates a new `BlockTemplate` for that ID,
    /// and then returns the ID.
    pub fn get_template_id(
        &self, 
        trie_store: Arc<TrieDBStore>,
        storage: Arc<Storage>,
        vicinity: Vicinity,
        state_root: H256,
        block_base_fee: U256,
    ) -> Result<u64> {
        let mut rng = rand::thread_rng();
        loop {
            let template_id = rng.gen();
            // Safety check to disallow 0 as it's equivalent to no template_id
            if template_id == 0 {
                continue;
            };
            let mut write_guard = self.templates.write().unwrap();

            if let std::collections::hash_map::Entry::Vacant(e) = write_guard.entry(template_id) {
                e.insert(BlockTemplate::new(trie_store, storage, vicinity, state_root, block_base_fee)?);
                return Ok(template_id);
            }
        }
    }

    /// Try to remove and return the `BlockTemplate` associated with the provided
    /// block template ID.
    pub fn remove(&self, template_id: u64) -> Option<BlockTemplate> {
        self.templates.write().unwrap().remove(&template_id)
    }

    /// Clears the `BlockTemplate` vector associated with the provided block template ID.
    pub fn clear(&self, template_id: u64) -> Result<(), TemplateError> {
        self.templates
            .read()
            .unwrap()
            .get(&template_id)
            .ok_or(TemplateError::NoSuchID)
            .map(BlockTemplate::clear)
    }

    /// Attempts to add a new transaction to the `BlockTemplate` associated with the
    /// provided template ID. If the transaction is a `SignedTx`, it also updates the
    /// corresponding account's nonce.
    /// Nonces for each account's transactions must be in strictly increasing order. This means that if the last
    /// included transaction for an account has nonce 3, the next one should have nonce 4. If a `SignedTx` with a nonce
    /// that is not one more than the previous nonce is added, an error is returned. This helps to ensure the integrity
    /// of the transactions inside the block template and enforce correct nonce usage.
    ///
    /// # Errors
    ///
    /// Returns `TemplateError::NoSuchID` if no block template is associated with the given template ID.
    /// Returns `TemplateError::InvalidNonce` if a `SignedTx` is provided with a nonce that is not one more than the
    /// previous nonce of transactions from the same sender in the block template.
    ///
    pub fn add_tx(
        &self,
        template_id: u64,
        tx: BlockTx,
        hash: NativeTxHash,
        gas_used: U256,
        base_fee: U256,
    ) -> Result<(), TemplateError> {
        self.templates
            .read()
            .unwrap()
            .get(&template_id)
            .ok_or(TemplateError::NoSuchID)
            .map(|template| template.add_tx(tx, hash, gas_used, base_fee))?
    }

    /// `drain_all` returns all transactions from the `BlockTemplate` associated with the
    /// provided template ID, removing them from the block. Transactions are returned in the
    /// order they were added.
    pub fn drain_all(&self, template_id: u64) -> Vec<BlockTxItem> {
        self.templates
            .read()
            .unwrap()
            .get(&template_id)
            .map_or(Vec::new(), BlockTemplate::drain_all)
    }

    pub fn get_cloned_vec(&self, template_id: u64) -> Vec<BlockTxItem> {
        self.templates
            .read()
            .unwrap()
            .get(&template_id)
            .map_or(Vec::new(), BlockTemplate::get_cloned_vec)
    }

    pub fn count(&self, template_id: u64) -> usize {
        self.templates
            .read()
            .unwrap()
            .get(&template_id)
            .map_or(0, BlockTemplate::len)
    }

    /// Removes all transactions in the block template whose sender matches the provided sender address.
    /// # Errors
    ///
    /// Returns `TemplateError::NoSuchID` if no block is associated with the given template ID.
    ///
    pub fn remove_txs_by_sender(&self, template_id: u64, sender: H160) -> Result<(), TemplateError> {
        self.templates
            .read()
            .unwrap()
            .get(&template_id)
            .ok_or(TemplateError::NoSuchID)
            .map(|template| template.remove_txs_by_sender(sender))
    }

    /// `get_next_valid_nonce` returns the next valid nonce for the account with the provided address
    /// in the `BlocKTemplate` associated with the provided block template ID. This method assumes that
    /// only signed transactions (which include a nonce) are added to the template using `add_tx`
    /// and that their nonces are in increasing order.
    pub fn get_next_valid_nonce(&self, template_id: u64, address: H160) -> Option<U256> {
        self.templates
            .read()
            .unwrap()
            .get(&template_id)
            .and_then(|template| template.get_next_valid_nonce(address))
    }

    pub fn get_total_gas_used(&self, template_id: u64) -> Option<U256> {
        self.templates
            .read()
            .unwrap()
            .get(&template_id)
            .map(|template| template.get_total_gas_used())
    }

    pub fn get_total_fees(&self, template_id: u64) -> Option<U256> {
        self.templates
            .read()
            .unwrap()
            .get(&template_id)
            .map(|template| template.get_total_fees())
    }
}

#[derive(Debug, Clone)]
pub enum BlockTx {
    SignedTx(Box<SignedTx>),
    SystemTx(SystemTx),
}

#[derive(Debug, Clone)]
pub struct BlockTxItem {
    pub block_tx: BlockTx,
    pub tx_hash: NativeTxHash,
    pub tx_fee: U256,
    pub gas_used: U256,
}

/// The `BlockTemplateData` holds the state root of the parent block, the block base fee,
/// the transactions to be included with a map of the account nonces, the total gas fees
/// and the total gas used by the transactions. It's used to create a block template by
/// adding transactions for different accounts.
///
#[derive(Debug, Default)]
struct BlockTemplateData {
    state_root: H256,
    block_base_fee: U256,
    transactions: Vec<BlockTxItem>,
    account_nonces: HashMap<H160, U256>,
    total_fees: U256,
    total_gas_used: U256,
}

impl BlockTemplateData {
    pub fn new(
        state_root: H256,
        block_base_fee: U256,
    ) -> Self {
        Self {
            state_root,
            block_base_fee,
            transactions: Vec::new(),
            account_nonces: HashMap::new(),
            total_fees: U256::zero(),
            total_gas_used: U256::zero(),
        }
    }
}

#[derive(Debug, Default)]
pub struct BlockTemplate {
    backend: Arc<EVMBackend>,
    data: Mutex<BlockTemplateData>,
}

impl BlockTemplate {
    fn new(
        trie_store: Arc<TrieDBStore>,
        storage: Arc<Storage>,
        vicinity: Vicinity,
        state_root: H256,
        block_base_fee: U256,
    ) -> Result<Self> {
        let state = trie_store
        .trie_db
        .trie_restore(&[0], None, state_root.into())
        .map_err(|e| EVMBackendError::TrieRestoreFailed(e.to_string()))?;

        Ok(Self {
            backend: Arc::new(EVMBackend {
                state,
                trie_store,
                storage,
                vicinity,
            }),
            data: Mutex::new(BlockTemplateData::new(state_root, block_base_fee))?,
        })
    }

    /// Clear the block template and reset backend state to parent state root, retaining
    /// parent state root and block base fee.
    pub fn clear(&self) {
        self.backend.reset(data.state_root);
        let mut data = self.data.lock().unwrap();
        data.account_nonces.clear();
        data.total_fees = U256::zero();
        data.total_gas_used = U256::zero();
        data.transactions.clear();
    }

    /// Drain the block template, retaining parent state root and block base fee and 
    /// return the associated transactions.
    pub fn drain_all(&self) -> Vec<BlockTxItem> {
        self.backend.reset(data.state_root);
        let mut data = self.data.lock().unwrap();
        data.account_nonces.clear();
        data.total_fees = U256::zero();
        data.total_gas_used = U256::zero();
        data.transactions.drain(..).collect::<Vec<BlockTxItem>>()
    }

    pub fn get_cloned_vec(&self) -> Vec<BlockTxItem> {
        self.data.lock().unwrap().transactions.clone()
    }

    pub fn add_tx(
        &self,
        tx: BlockTx,
        tx_hash: NativeTxHash,
        gas_used: U256,
        base_fee: U256,
    ) -> Result<(), TemplateError> {
        let mut gas_fee = U256::zero();
        let mut data = self.data.lock().unwrap();
        if let BlockTx::SignedTx(signed_tx) = &tx {
            // Validate tx nonce
            if let Some(nonce) = data.account_nonces.get(&signed_tx.sender) {
                if signed_tx.nonce() != nonce + 1 {
                    return Err(TemplateError::InvalidNonce((signed_tx.clone(), *nonce)));
                }
            }
            data.account_nonces
                .insert(signed_tx.sender, signed_tx.nonce());

            // Update block total gas used and total fees
            gas_fee = match calculate_gas_fee(signed_tx, gas_used, base_fee) {
                Ok(fee) => fee,
                Err(_) => return Err(TemplateError::InvalidFee),
            };
            data.total_fees += gas_fee;
            data.total_gas_used += gas_used;
        }
        data.transactions.push(BlockTxItem {
            block_tx: tx,
            tx_hash,
            tx_fee: gas_fee,
            gas_used,
        });
        Ok(())
    }

    pub fn len(&self) -> usize {
        self.data.lock().unwrap().transactions.len()
    }

    pub fn is_empty(&self) -> bool {
        self.data.lock().unwrap().transactions.is_empty()
    }

    pub fn remove_txs_by_sender(&self, sender: H160) {
        let mut data = self.data.lock().unwrap();
        let mut fees_to_remove = U256::zero();
        let mut gas_used_to_remove = U256::zero();
        data.transactions.retain(|item| {
            let tx_sender = match &item.block_tx {
                BlockTx::SignedTx(tx) => tx.sender,
                BlockTx::SystemTx(tx) => tx.sender().unwrap_or_default(),
            };
            if tx_sender == sender {
                fees_to_remove += item.tx_fee;
                gas_used_to_remove += item.gas_used;
                return false;
            }
            true
        });
        data.total_fees -= fees_to_remove;
        data.total_gas_used -= gas_used_to_remove;
        data.account_nonces.remove(&sender);
    }

    pub fn get_next_valid_nonce(&self, address: H160) -> Option<U256> {
        self.data
            .lock()
            .unwrap()
            .account_nonces
            .get(&address)
            .map(ToOwned::to_owned)
            .map(|nonce| nonce + 1)
    }

    pub fn get_total_fees(&self) -> U256 {
        self.data.lock().unwrap().total_fees
    }

    pub fn get_total_gas_used(&self) -> U256 {
        self.data.lock().unwrap().total_gas_used
    }
}

impl From<SignedTx> for BlockTx {
    fn from(tx: SignedTx) -> Self {
        Self::SignedTx(Box::new(tx))
    }
}

#[derive(Debug)]
pub enum TemplateError {
    NoSuchID,
    InvalidNonce((Box<SignedTx>, U256)),
    InvalidFee,
}

impl std::fmt::Display for TemplateError {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            TemplateError::NoSuchID => write!(f, "No block template for this template id"),
            TemplateError::InvalidNonce((tx, nonce)) => write!(f, "Invalid nonce {:x?} for tx {:x?}. Previous included nonce is {}. TXs should be included in increasing nonce order.", tx.nonce(), tx.transaction.hash(), nonce),
            TemplateError::InvalidFee => write!(f, "Invalid transaction fee from value overflow"),
        }
    }
}

impl std::error::Error for TemplateError {}

#[cfg(test_off)]
mod tests {
    use std::str::FromStr;

    use ethereum_types::{H256, U256};

    use crate::transaction::bridge::BalanceUpdate;

    use super::*;

    #[test]
    fn test_invalid_nonce_order() -> Result<(), TemplateError> {
        let template = BlockTemplate::new();

        // Nonce 2, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx1 = BlockTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0adb0386f95848d33b49ee6057c34e530f87f696a29b4e1b04ef90b2a58bbedbca02f500cf29c5c4245608545e7d9d35b36ef0365e5c52d96e69b8f07920d32552f").unwrap()));

        // Nonce 2, sender 0x6bc42fd533d6cb9d973604155e1f7197a3b0e703
        let tx2 = BlockTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa09588b47d2cd3f474d6384309cca5cb8e360cb137679f0a1589a1c184a15cb27ca0453ddbf808b83b279cac3226b61a9d83855aba60ae0d3a8407cba0634da7459d").unwrap()));

        // Nonce 0, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx3 = BlockTx::SignedTx(Box::new(SignedTx::try_from("f869808502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa03d28d24808c3de08c606c5544772ded91913f648ad56556f181905208e206c85a00ecd0ba938fb89fc4a17ea333ea842c7305090dee9236e2b632578f9e5045cb3").unwrap()));

        template.add_tx(
            tx1,
            H256::from_low_u64_be(1).into(),
            U256::zero(),
            U256::zero(),
        )?;
        template.add_tx(
            tx2,
            H256::from_low_u64_be(2).into(),
            U256::zero(),
            U256::zero(),
        )?;
        // Should fail as nonce 2 is already included for this sender
        let res = template.add_tx(
            tx3,
            H256::from_low_u64_be(3).into(),
            U256::zero(),
            U256::zero(),
        );
        assert!(matches!(res, Err(TemplateError::InvalidNonce { .. })));
        Ok(())
    }

    #[test]
    fn test_invalid_nonce_order_with_transfer_domain() -> Result<(), TemplateError> {
        let template = BlockTemplate::new();

        // Nonce 2, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx1 = BlockTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0adb0386f95848d33b49ee6057c34e530f87f696a29b4e1b04ef90b2a58bbedbca02f500cf29c5c4245608545e7d9d35b36ef0365e5c52d96e69b8f07920d32552f").unwrap()));

        // Nonce 2, sender 0x6bc42fd533d6cb9d973604155e1f7197a3b0e703
        let tx2 = BlockTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa09588b47d2cd3f474d6384309cca5cb8e360cb137679f0a1589a1c184a15cb27ca0453ddbf808b83b279cac3226b61a9d83855aba60ae0d3a8407cba0634da7459d").unwrap()));

        // sender 0x6bc42fd533d6cb9d973604155e1f7197a3b0e703
        let tx3 = BlockTx::BridgeTx(BridgeTx::EvmIn(BalanceUpdate {
            address: H160::from_str("0x6bc42fd533d6cb9d973604155e1f7197a3b0e703").unwrap(),
            amount: U256::one(),
        }));

        // Nonce 0, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx4 = BlockTx::SignedTx(Box::new(SignedTx::try_from("f869808502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa03d28d24808c3de08c606c5544772ded91913f648ad56556f181905208e206c85a00ecd0ba938fb89fc4a17ea333ea842c7305090dee9236e2b632578f9e5045cb3").unwrap()));

        template.add_tx(
            tx1,
            H256::from_low_u64_be(1).into(),
            U256::zero(),
            U256::zero(),
        )?;
        template.add_tx(
            tx2,
            H256::from_low_u64_be(2).into(),
            U256::zero(),
            U256::zero(),
        )?;
        template.add_tx(
            tx3,
            H256::from_low_u64_be(3).into(),
            U256::zero(),
            U256::zero(),
        )?;
        // Should fail as nonce 2 is already included for this sender
        let res = template.add_tx(
            tx4,
            H256::from_low_u64_be(4).into(),
            U256::zero(),
            U256::zero(),
        );
        assert!(matches!(res, Err(TemplateError::InvalidNonce { .. })));
        Ok(())
    }

    #[test]
    fn test_valid_nonce_order() -> Result<(), TemplateError> {
        let template = BlockTemplate::new();

        // Nonce 0, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx1 = BlockTx::SignedTx(Box::new(SignedTx::try_from("f869808502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa03d28d24808c3de08c606c5544772ded91913f648ad56556f181905208e206c85a00ecd0ba938fb89fc4a17ea333ea842c7305090dee9236e2b632578f9e5045cb3").unwrap()));

        // Nonce 1, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx2 = BlockTx::SignedTx(Box::new(SignedTx::try_from("f869018502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0dd1fad9a8465969354d567e8a74af3f6de3e56abbe1b71154d7929d0bd5cc170a0353190adb50e3cfae82a77c2ea56b49a86f72bd0071a70d1c25c49827654aa68").unwrap()));

        // Nonce 2, sender 0xe61a3a6eb316d773c773f4ce757a542f673023c6
        let tx3 = BlockTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa0adb0386f95848d33b49ee6057c34e530f87f696a29b4e1b04ef90b2a58bbedbca02f500cf29c5c4245608545e7d9d35b36ef0365e5c52d96e69b8f07920d32552f").unwrap()));

        // Nonce 2, sender 0x6bc42fd533d6cb9d973604155e1f7197a3b0e703
        let tx4 = BlockTx::SignedTx(Box::new(SignedTx::try_from("f869028502540be400832dc6c0943e338e722607a8c1eab615579ace4f6dedfa19fa80840adb1a9a2aa09588b47d2cd3f474d6384309cca5cb8e360cb137679f0a1589a1c184a15cb27ca0453ddbf808b83b279cac3226b61a9d83855aba60ae0d3a8407cba0634da7459d").unwrap()));

        template.add_tx(
            tx1,
            H256::from_low_u64_be(1).into(),
            U256::zero(),
            U256::zero(),
        )?;
        template.add_tx(
            tx2,
            H256::from_low_u64_be(2).into(),
            U256::zero(),
            U256::zero(),
        )?;
        template.add_tx(
            tx3,
            H256::from_low_u64_be(3).into(),
            U256::zero(),
            U256::zero(),
        )?;
        template.add_tx(
            tx4,
            H256::from_low_u64_be(4).into(),
            U256::zero(),
            U256::zero(),
        )?;
        Ok(())
    }
}
