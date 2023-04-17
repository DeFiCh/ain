use crate::codegen::rpc::{
    ffi::{
        EthAccountsResult, EthBlockInfo, EthBlockNumberResult, EthCallInput, EthCallResult,
        EthChainIdResult, EthGetBalanceInput, EthGetBalanceResult, EthGetBlockByHashInput,
        EthGetBlockByNumberInput, EthGetBlockTransactionCountByHashInput,
        EthGetBlockTransactionCountByHashResult, EthGetBlockTransactionCountByNumberInput,
        EthGetBlockTransactionCountByNumberResult, EthGetCodeInput, EthGetCodeResult,
        EthGetStorageAtInput, EthGetStorageAtResult, EthGetTransactionByBlockHashAndIndexInput,
        EthGetTransactionByBlockNumberAndIndexInput, EthGetTransactionByHashInput,
        EthGetTransactionCountInput, EthGetTransactionCountResult, EthMiningResult,
        EthSendRawTransactionInput, EthSendRawTransactionResult, EthTransactionInfo,
    },
    EthService,
};
use ain_cpp_imports::publish_eth_transaction;
use ain_evm::handler::Handlers;
use primitive_types::{H256, U256};
use std::convert::Into;
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

    fn Eth_GetTransactionByHash(
        handler: Arc<Handlers>,
        input: EthGetTransactionByHashInput,
    ) -> Result<EthTransactionInfo, jsonrpsee_core::Error>;

    fn Eth_GetTransactionByBlockHashAndIndex(
        handler: Arc<Handlers>,
        input: EthGetTransactionByBlockHashAndIndexInput,
    ) -> Result<EthTransactionInfo, jsonrpsee_core::Error>;

    fn Eth_GetTransactionByBlockNumberAndIndex(
        handler: Arc<Handlers>,
        input: EthGetTransactionByBlockNumberAndIndexInput,
    ) -> Result<EthTransactionInfo, jsonrpsee_core::Error>;

    fn Eth_GetBlockTransactionCountByHash(
        handler: Arc<Handlers>,
        input: EthGetBlockTransactionCountByHashInput,
    ) -> Result<EthGetBlockTransactionCountByHashResult, jsonrpsee_core::Error>;

    fn Eth_GetBlockTransactionCountByNumber(
        handler: Arc<Handlers>,
        input: EthGetBlockTransactionCountByNumberInput,
    ) -> Result<EthGetBlockTransactionCountByNumberResult, jsonrpsee_core::Error>;

    fn Eth_GetCode(
        handler: Arc<Handlers>,
        input: EthGetCodeInput,
    ) -> Result<EthGetCodeResult, jsonrpsee_core::Error>;

    fn Eth_GetStorageAt(
        handler: Arc<Handlers>,
        input: EthGetStorageAtInput,
    ) -> Result<EthGetStorageAtResult, jsonrpsee_core::Error>;

    fn Eth_SendRawTransaction(
        handler: Arc<Handlers>,
        input: EthSendRawTransactionInput,
    ) -> Result<EthSendRawTransactionResult, jsonrpsee_core::Error>;

    fn Eth_GetTransactionCount(
        handler: Arc<Handlers>,
        input: EthGetTransactionCountInput,
    ) -> Result<EthGetTransactionCountResult, jsonrpsee_core::Error>;
}

#[allow(non_snake_case)]
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
        let value: U256 = value.parse().expect("Wrong value format");

        let (_, data) = handler
            .evm
            .call(from, to, value, data.as_bytes(), gas, vec![]);

        Ok(EthCallResult {
            data: String::from_utf8_lossy(&*data).to_string(),
        })
    }

    fn Eth_Accounts(_handler: Arc<Handlers>) -> Result<EthAccountsResult, jsonrpsee_core::Error> {
        let accounts = ain_cpp_imports::get_accounts().unwrap();
        Ok(EthAccountsResult { accounts })
    }

    fn Eth_GetBalance(
        handler: Arc<Handlers>,
        input: EthGetBalanceInput,
    ) -> Result<EthGetBalanceResult, jsonrpsee_core::Error> {
        let EthGetBalanceInput { address, .. } = input;
        let address = address.parse().expect("Wrong address");
        let balance = handler.evm.get_balance(address);

        Ok(EthGetBalanceResult {
            balance: balance.to_string(),
        })
    }

    fn Eth_GetBlockByHash(
        handler: Arc<Handlers>,
        input: EthGetBlockByHashInput,
    ) -> Result<EthBlockInfo, jsonrpsee_core::Error> {
        let EthGetBlockByHashInput { hash, .. } = input;

        let hash: H256 = hash.parse().expect("Invalid hash");

        handler
            .storage
            .get_block_by_hash(&hash)
            .map(Into::into)
            .ok_or(jsonrpsee_core::Error::Custom(String::from("Missing block")))
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
        let count = handler
            .storage
            .get_latest_block()
            .map(|block| block.header.number)
            .unwrap_or_default();

        Ok(EthBlockNumberResult {
            block_number: format!("0x{:x}", count),
        })
    }

    fn Eth_GetBlockByNumber(
        handler: Arc<Handlers>,
        input: EthGetBlockByNumberInput,
    ) -> Result<EthBlockInfo, jsonrpsee_core::Error> {
        let EthGetBlockByNumberInput { number, .. } = input;

        let number: U256 = number.parse().ok().unwrap();
        handler
            .storage
            .get_block_by_number(&number)
            .map(Into::into)
            .ok_or(jsonrpsee_core::Error::Custom(String::from("Missing block")))
    }

    fn Eth_Mining(_handler: Arc<Handlers>) -> Result<EthMiningResult, jsonrpsee_core::Error> {
        let mining = ain_cpp_imports::is_mining().unwrap();

        Ok(EthMiningResult { is_mining: mining })
    }

    fn Eth_GetTransactionByHash(
        handler: Arc<Handlers>,
        input: EthGetTransactionByHashInput,
    ) -> Result<EthTransactionInfo, jsonrpsee_core::Error> {
        let hash: H256 = input.hash.parse().ok().unwrap();
        handler
            .storage
            .get_transaction_by_hash(hash)
            .and_then(|tx| tx.try_into().ok())
            .ok_or(jsonrpsee_core::Error::Custom(String::from(
                "Missing transaction",
            )))
    }

    fn Eth_GetTransactionByBlockHashAndIndex(
        handler: Arc<Handlers>,
        input: EthGetTransactionByBlockHashAndIndexInput,
    ) -> Result<EthTransactionInfo, jsonrpsee_core::Error> {
        let hash: H256 = input.block_hash.parse().ok().unwrap();
        let index: usize = input.index.parse().ok().unwrap();
        handler
            .storage
            .get_transaction_by_block_hash_and_index(hash, index)
            .and_then(|tx| tx.try_into().ok())
            .ok_or(jsonrpsee_core::Error::Custom(String::from(
                "Missing transaction",
            )))
    }

    fn Eth_GetTransactionByBlockNumberAndIndex(
        handler: Arc<Handlers>,
        input: EthGetTransactionByBlockNumberAndIndexInput,
    ) -> Result<EthTransactionInfo, jsonrpsee_core::Error> {
        let number: U256 = input.block_number.parse().ok().unwrap();
        let index: usize = input.index.parse().ok().unwrap();
        handler
            .storage
            .get_transaction_by_block_number_and_index(number, index)
            .and_then(|tx| tx.try_into().ok())
            .ok_or(jsonrpsee_core::Error::Custom(String::from(
                "Missing transaction",
            )))
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
            number_transaction: format!("{:#x}", count),
        })
    }

    fn Eth_GetBlockTransactionCountByNumber(
        handler: Arc<Handlers>,
        input: EthGetBlockTransactionCountByNumberInput,
    ) -> Result<EthGetBlockTransactionCountByNumberResult, jsonrpsee_core::Error> {
        let EthGetBlockTransactionCountByNumberInput { block_number } = input;

        let number: usize = block_number.parse().ok().unwrap();
        let block = handler.block.get_block_by_number(number).unwrap();
        let count = block.transactions.len();

        Ok(EthGetBlockTransactionCountByNumberResult {
            number_transaction: format!("{:#x}", count),
        })
    }

    fn Eth_GetCode(
        handler: Arc<Handlers>,
        input: EthGetCodeInput,
    ) -> Result<EthGetCodeResult, jsonrpsee_core::Error> {
        let EthGetCodeInput { address, .. } = input;

        let address = address.parse().expect("Invalid address");
        let code = handler.evm.get_code(address);

        if code.len() == 0 {
            return Ok(EthGetCodeResult {
                code: String::from("0x"),
            });
        }

        Ok(EthGetCodeResult {
            code: hex::encode(code),
        })
    }

    fn Eth_GetStorageAt(
        handler: Arc<Handlers>,
        input: EthGetStorageAtInput,
    ) -> Result<EthGetStorageAtResult, jsonrpsee_core::Error> {
        let EthGetStorageAtInput {
            address, position, ..
        } = input;

        let address = address.parse().expect("Invalid address");
        let position = position.parse().expect("Invalid postition");

        let storage = handler.evm.get_storage(address);
        let &value = storage.get(&position).unwrap_or(&H256::zero());

        Ok(EthGetStorageAtResult {
            value: format!("{:#x}", value),
        })
    }

    fn Eth_SendRawTransaction(
        _handler: Arc<Handlers>,
        input: EthSendRawTransactionInput,
    ) -> Result<EthSendRawTransactionResult, jsonrpsee_core::Error> {
        let EthSendRawTransactionInput { transaction } = input;

        let hex = hex::decode(transaction).expect("Invalid transaction");
        let stat = publish_eth_transaction(hex).unwrap();

        println!("{stat}");

        Ok(EthSendRawTransactionResult {
            hash: format!("{stat}"),
        })
    }

    fn Eth_GetTransactionCount(
        handler: Arc<Handlers>,
        input: EthGetTransactionCountInput,
    ) -> Result<EthGetTransactionCountResult, jsonrpsee_core::Error> {
        let EthGetTransactionCountInput { address, .. } = input;

        let address = address.parse().expect("Invalid address");
        let nonce = handler.evm.get_nonce(address);

        Ok(EthGetTransactionCountResult {
            number_transaction: format!("{:#x}", nonce),
        })
    }
}
