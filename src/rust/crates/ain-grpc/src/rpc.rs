use crate::codegen::rpc::{
    ffi::{
        EthAccountsResult, EthCallInput, EthCallResult, EthGetBalanceInput, EthGetBalanceResult,
        EthTransactionInfo,
    },
    EthService,
};
use ain_evm_state::handler::EVMHandler;
use std::sync::Arc;

pub trait EthServiceApi {
    // Read only call
    fn Eth_Call(
        handler: Arc<EVMHandler>,
        input: EthCallInput,
    ) -> Result<EthCallResult, jsonrpsee_core::Error>;

    fn Eth_Accounts(handler: Arc<EVMHandler>) -> Result<EthAccountsResult, jsonrpsee_core::Error>;

    fn Eth_GetBalance(
        handler: Arc<EVMHandler>,
        input: EthGetBalanceInput,
    ) -> Result<EthGetBalanceResult, jsonrpsee_core::Error>;
}

impl EthServiceApi for EthService {
    fn Eth_Call(
        handler: Arc<EVMHandler>,
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
        let (_, data) = handler.call_evm(from, to, value.into(), data.into(), gas, vec![]);

        Ok(EthCallResult {
            data: String::from_utf8_lossy(&*data).to_string(),
        })
    }

    fn Eth_Accounts(handler: Arc<EVMHandler>) -> Result<EthAccountsResult, jsonrpsee_core::Error> {
        // Get from wallet
        Ok(EthAccountsResult { accounts: vec![] })
    }
    fn Eth_GetBalance(
        handler: Arc<EVMHandler>,
        input: EthGetBalanceInput,
    ) -> Result<EthGetBalanceResult, jsonrpsee_core::Error> {
        let EthGetBalanceInput {
            address,
            block_number,
        } = input;
        let address = address.parse().expect("Wrong address");
        let balance = handler
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
}
