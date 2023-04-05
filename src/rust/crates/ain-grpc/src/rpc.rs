use crate::codegen::rpc::{
    ffi::{
        EthAccountsResult, EthCallInput, EthCallResult, EthGetBalanceInput, EthGetBalanceResult,
        EthTransactionInfo, EthGetBlockByHashInput
    },
    EthService,
};
use ain_evm_state::handler::{Handlers};
use std::sync::Arc;
use ethereum::BlockAny;

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

    fn eth_getBlockByHash(
        handler: Arc<Handlers>,
        input: EthGetBlockByHashInput
    ) -> Result<BlockAny, jsonrpsee_core::Error>;
}

impl EthServiceApi for EthService {
    fn Eth_Call(
        handler: Arc<Handlers>,
        input: EthCallInput,
    ) -> Result<EthCallResult, jsonrpsee_core::Error> {
        let EthCallInput {
            transaction_info,
            block_number,
        } = input;
        let EthTransactionInfo {
            from,
            to,
            gas,
            price,
            value,
            data,
            nonce,
        } = transaction_info;

        let from = from.parse().expect("Invalid from address");
        let to = to.parse().expect("Invalid to address");
        let (_, data) = handler.evm.call_evm(from, to, value.into(), data.into(), gas, vec![]);

        Ok(EthCallResult {
            data: String::from_utf8_lossy(&*data).to_string(),
        })
    }

    fn Eth_Accounts(handler: Arc<Handlers>) -> Result<EthAccountsResult, jsonrpsee_core::Error> {
        // Get from wallet
        Ok(EthAccountsResult { accounts: vec![] })
    }
    fn Eth_GetBalance(
        handler: Arc<Handlers>,
        input: EthGetBalanceInput,
    ) -> Result<EthGetBalanceResult, jsonrpsee_core::Error> {
        let EthGetBalanceInput {
            address,
            block_number,
        } = input;
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

    fn eth_getBlockByHash(
        handler: Arc<Handlers>,
        input: EthGetBlockByHashInput
    ) -> Result<BlockAny, jsonrpsee_core::Error> {
        let EthGetBlockByHashInput {
            hash,
            ..
        } = input;

        let hash = hash.parse().expect("Invalid hash");
        let block = handler.block.get_block_hash(hash).unwrap();

        Ok(block)
    }
}
