use crate::codegen::rpc::{
    ffi::{
        EthAccountsResult, EthBlockInfo, EthBlockNumberResult, EthCallInput, EthCallResult,
        EthChainIdResult, EthGetBalanceInput, EthGetBalanceResult, EthGetBlockByHashInput,
        EthGetBlockByNumberInput, EthGetBlockTransactionCountByHashInput,
        EthGetBlockTransactionCountByHashResult, EthGetBlockTransactionCountByNumberInput,
        EthGetBlockTransactionCountByNumberResult, EthMiningResult, EthTransactionInfo,
    },
    EthService,
};
use ain_evm::handler::Handlers;
use primitive_types::{H256, U256};
use std::mem::size_of_val;
use std::sync::Arc;

#[allow(non_snake_case)]
pub trait EthServiceApi {
    // Read only call
    fn Eth_Call(
        handler: Arc<Handlers>,
        input: EthCallInput,
    ) -> Result<EthCallResult, jsonrpsee_core::Error>;

    fn Eth_Accounts(handler: Arc<Handlers>) -> Result<EthAccountsResult, jsonrpsee_core::Error>;

    fn Eth_GetBalance(
        handler: Arc<Handlers>,
        input: EthGetBalanceInput,
    ) -> Result<EthGetBalanceResult, jsonrpsee_core::Error>;

    fn Eth_GetBlockByHash(
        handler: Arc<Handlers>,
        input: EthGetBlockByHashInput,
    ) -> Result<EthBlockInfo, jsonrpsee_core::Error>;

    fn Eth_ChainId(_handler: Arc<Handlers>) -> Result<EthChainIdResult, jsonrpsee_core::Error>;

    fn Net_Version(_handler: Arc<Handlers>) -> Result<EthChainIdResult, jsonrpsee_core::Error>;

    fn Eth_BlockNumber(
        handler: Arc<Handlers>,
    ) -> Result<EthBlockNumberResult, jsonrpsee_core::Error>;

    fn Eth_GetBlockByNumber(
        handler: Arc<Handlers>,
        input: EthGetBlockByNumberInput,
    ) -> Result<EthBlockInfo, jsonrpsee_core::Error>;

    fn Eth_Mining(handler: Arc<Handlers>) -> Result<EthMiningResult, jsonrpsee_core::Error>;

    fn Eth_GetBlockTransactionCountByHash(
        handler: Arc<Handlers>,
        input: EthGetBlockTransactionCountByHashInput,
    ) -> Result<EthGetBlockTransactionCountByHashResult, jsonrpsee_core::Error>;

    fn Eth_GetBlockTransactionCountByNumber(
        handler: Arc<Handlers>,
        input: EthGetBlockTransactionCountByNumberInput,
    ) -> Result<EthGetBlockTransactionCountByNumberResult, jsonrpsee_core::Error>;
}

impl EthServiceApi for EthService {
    fn Eth_Call(
        handler: Arc<Handlers>,
        input: EthCallInput,
    ) -> Result<EthCallResult, jsonrpsee_core::Error> {
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
        } = transaction_info;

        let from = from.parse().ok();
        let to = to.parse().ok();
        let (_, data) = handler
            .evm
            .call(from, to, value.into(), data.as_bytes(), gas, vec![]);

        Ok(EthCallResult {
            data: String::from_utf8_lossy(&*data).to_string(),
        })
    }

    fn Eth_Accounts(_handler: Arc<Handlers>) -> Result<EthAccountsResult, jsonrpsee_core::Error> {
        // Get from wallet
        Ok(EthAccountsResult { accounts: vec![] })
    }

    fn Eth_GetBalance(
        handler: Arc<Handlers>,
        input: EthGetBalanceInput,
    ) -> Result<EthGetBalanceResult, jsonrpsee_core::Error> {
        let EthGetBalanceInput { address, .. } = input;
        let address = address.parse().expect("Wrong address");
        let balance = handler
            .evm
            .state
            .read()
            .unwrap()
            .get(&address)
            .map(|addr| addr.balance)
            .unwrap_or_default();

        Ok(EthGetBalanceResult {
            balance: balance.to_string(),
        })
    }

    fn Eth_GetBlockByHash(
        handler: Arc<Handlers>,
        input: EthGetBlockByHashInput,
    ) -> Result<EthBlockInfo, jsonrpsee_core::Error> {
        let EthGetBlockByHashInput { hash, .. } = input;

        let hash = hash.parse().expect("Invalid hash");
        let block = handler.block.get_block_by_hash(hash).unwrap();

        Ok(EthBlockInfo {
            block_number: format!("{:#x}", block.header.number),
            hash: format_hash(block.header.hash()),
            parent_hash: format_hash(block.header.parent_hash),
            nonce: format!("{:#x}", block.header.nonce),
            sha3_uncles: format_hash(block.header.ommers_hash),
            logs_bloom: format!("{:#x}", block.header.logs_bloom),
            transactions_root: format_hash(block.header.transactions_root),
            state_root: format_hash(block.header.state_root),
            receipt_root: format_hash(block.header.receipts_root),
            miner: format!("{:#x}", block.header.beneficiary),
            difficulty: format!("{:#x}", block.header.difficulty),
            total_difficulty: format_number(block.header.difficulty),
            extra_data: format!("{:#x?}", block.header.extra_data.to_ascii_lowercase()),
            size: format!("{:#x}", size_of_val(&block)),
            gas_limit: format_number(block.header.gas_limit),
            gas_used: format_number(block.header.gas_used),
            timestamps: format!("0x{:x}", block.header.timestamp),
            transactions: block
                .transactions
                .iter()
                .map(|x| x.hash().to_string())
                .collect::<Vec<String>>(),
            uncles: block
                .ommers
                .iter()
                .map(|x| x.hash().to_string())
                .collect::<Vec<String>>(),
        })
    }

    fn Eth_ChainId(_handler: Arc<Handlers>) -> Result<EthChainIdResult, jsonrpsee_core::Error> {
        let chain_id = ain_cpp_imports::get_chain_id().unwrap();

        Ok(EthChainIdResult {
            id: format!("{:#x}", chain_id),
        })
    }

    fn Net_Version(_handler: Arc<Handlers>) -> Result<EthChainIdResult, jsonrpsee_core::Error> {
        let chain_id = ain_cpp_imports::get_chain_id().unwrap();

        Ok(EthChainIdResult {
            id: format!("{}", chain_id),
        })
    }

    fn Eth_BlockNumber(
        handler: Arc<Handlers>,
    ) -> Result<EthBlockNumberResult, jsonrpsee_core::Error> {
        let count = handler.block.blocks.read().unwrap().len();

        Ok(EthBlockNumberResult {
            block_number: format!("0x{:x}", count),
        })
    }

    fn Eth_GetBlockByNumber(
        handler: Arc<Handlers>,
        input: EthGetBlockByNumberInput,
    ) -> Result<EthBlockInfo, jsonrpsee_core::Error> {
        let EthGetBlockByNumberInput { number, .. } = input;

        let number: usize = number.parse().ok().unwrap();
        let block = handler.block.get_block_by_number(number).unwrap();

        Ok(EthBlockInfo {
            block_number: format!("{:#x}", block.header.number),
            hash: format_hash(block.header.hash()),
            parent_hash: format_hash(block.header.parent_hash),
            nonce: format!("{:#x}", block.header.nonce),
            sha3_uncles: format_hash(block.header.ommers_hash),
            logs_bloom: format!("{:#x}", block.header.logs_bloom),
            transactions_root: format_hash(block.header.transactions_root),
            state_root: format_hash(block.header.state_root),
            receipt_root: format_hash(block.header.receipts_root),
            miner: format!("{:#x}", block.header.beneficiary),
            difficulty: format!("{:#x}", block.header.difficulty),
            total_difficulty: format_number(block.header.difficulty),
            extra_data: format!("{:#x?}", block.header.extra_data.to_ascii_lowercase()),
            size: format!("{:#x}", size_of_val(&block)),
            gas_limit: format_number(block.header.gas_limit),
            gas_used: format_number(block.header.gas_used),
            timestamps: format!("{:#x}", block.header.timestamp),
            transactions: block
                .transactions
                .iter()
                .map(|x| x.hash().to_string())
                .collect::<Vec<String>>(),
            uncles: block
                .ommers
                .iter()
                .map(|x| x.hash().to_string())
                .collect::<Vec<String>>(),
        })
    }

    fn Eth_Mining(_handler: Arc<Handlers>) -> Result<EthMiningResult, jsonrpsee_core::Error> {
        let mining = ain_cpp_imports::is_mining().unwrap();

        Ok(EthMiningResult { is_mining: mining })
    }

    fn Eth_GetBlockTransactionCountByHash(
        handler: Arc<Handlers>,
        input: EthGetBlockTransactionCountByHashInput,
    ) -> Result<EthGetBlockTransactionCountByHashResult, jsonrpsee_core::Error> {
        let EthGetBlockTransactionCountByHashInput { block_hash } = input;

        let block_hash = block_hash.parse().expect("Invalid hash");
        let block = handler.block.get_block_by_hash(block_hash).unwrap();
        let count = block.transactions.len();

        Ok(EthGetBlockTransactionCountByHashResult {
            number_transaction: format!("0x{:x}", count),
        })
    }

    fn Eth_GetBlockTransactionCountByNumber(
        handler: Arc<Handlers>,
        input: EthGetBlockTransactionCountByNumberInput,
    ) -> Result<EthGetBlockTransactionCountByNumberResult, jsonrpsee_core::Error> {
        let EthGetBlockTransactionCountByNumberInput { block_number, .. } = input;

        let number: usize = block_number.parse().ok().unwrap();
        let block = handler.block.get_block_by_number(number).unwrap();
        let count = block.transactions.len();

        Ok(EthGetBlockTransactionCountByNumberResult {
            number_transaction: format!("0x{:x}", count),
        })
    }
}

fn format_hash(hash: H256) -> String {
    return format!("{:#x}", hash);
}

fn format_number(number: U256) -> String {
    return format!("{:#x}", number);
}
