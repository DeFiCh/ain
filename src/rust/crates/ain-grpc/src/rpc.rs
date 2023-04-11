use crate::codegen::rpc::{
    ffi::{
        EthAccountsResult, EthBlockInfo, EthBlockNumberResult, EthCallInput, EthCallResult,
        EthGetBalanceInput, EthGetBalanceResult, EthGetBlockByHashInput, EthGetBlockByHashResult,
        EthGetBlockByNumberInput, EthGetBlockByNumberResult, EthTransactionInfo,
    },
    EthService,
};
use ain_evm_state::handler::Handlers;
use std::mem::size_of_val;
use std::sync::Arc;

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
    ) -> Result<EthGetBlockByHashResult, jsonrpsee_core::Error>;

    fn Eth_BlockNumber(
        handler: Arc<Handlers>,
    ) -> Result<EthBlockNumberResult, jsonrpsee_core::Error>;

    fn Eth_GetBlockByCount(
        handler: Arc<Handlers>,
        input: EthGetBlockByNumberInput,
    ) -> Result<EthGetBlockByNumberResult, jsonrpsee_core::Error>;
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
    ) -> Result<EthGetBlockByHashResult, jsonrpsee_core::Error> {
        let EthGetBlockByHashInput { hash, .. } = input;

        let hash = hash.parse().expect("Invalid hash");
        let block = handler.block.get_block_by_hash(hash).unwrap();

        Ok(EthGetBlockByHashResult {
            block_info: EthBlockInfo {
                block_number: block.header.number.to_string(),
                hash: block.header.hash().to_string(),
                parent_hash: block.header.parent_hash.to_string(),
                nonce: block.header.nonce.to_string(),
                sha3_uncles: block.header.ommers_hash.to_string(),
                logs_bloom: block.header.logs_bloom.to_string(),
                transactions_root: block.header.transactions_root.to_string(),
                state_root: block.header.state_root.to_string(),
                receipt_root: block.header.receipts_root.to_string(),
                miner: block.header.beneficiary.to_string(),
                difficulty: block.header.difficulty.to_string(),
                total_difficulty: block.header.difficulty.to_string(),
                extra_data: String::from_utf8(block.header.extra_data.clone()).unwrap(),
                size: size_of_val(&block).to_string(),
                gas_limit: block.header.gas_limit.to_string(),
                gas_used: block.header.gas_used.to_string(),
                timestamps: block.header.timestamp.to_string(),
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
            },
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

    fn Eth_GetBlockByCount(
        handler: Arc<Handlers>,
        input: EthGetBlockByNumberInput,
    ) -> Result<EthGetBlockByNumberResult, jsonrpsee_core::Error> {
        let EthGetBlockByNumberInput { number, .. } = input;

        let number: usize = number.parse().ok().unwrap();
        let block = handler.block.get_block_by_number(number).unwrap();

        Ok(EthGetBlockByNumberResult {
            block_info: EthBlockInfo {
                block_number: block.header.number.to_string(),
                hash: block.header.hash().to_string(),
                parent_hash: block.header.parent_hash.to_string(),
                nonce: block.header.nonce.to_string(),
                sha3_uncles: block.header.ommers_hash.to_string(),
                logs_bloom: block.header.logs_bloom.to_string(),
                transactions_root: block.header.transactions_root.to_string(),
                state_root: block.header.state_root.to_string(),
                receipt_root: block.header.receipts_root.to_string(),
                miner: block.header.beneficiary.to_string(),
                difficulty: block.header.difficulty.to_string(),
                total_difficulty: block.header.difficulty.to_string(),
                extra_data: String::from_utf8(block.header.extra_data.clone()).unwrap(),
                size: size_of_val(&block).to_string(),
                gas_limit: block.header.gas_limit.to_string(),
                gas_used: block.header.gas_used.to_string(),
                timestamps: block.header.timestamp.to_string(),
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
            },
        })
    }
}
