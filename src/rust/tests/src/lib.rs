#[cfg(test)]
mod test {
    use anyhow::Result;
    use defi_rpc::json::{TokenInfo, VaultInfo};
    use defi_rpc::Client;
    use serde_json::json;

    #[test]
    fn calculate_loan_interest() -> Result<()> {
        let client = Client::from_env()?;
        let new_address = client.get_new_address()?;

        let loan_scheme_id = client.call::<String>(
            "createloanscheme",
            &[120.into(), 5.into(), "LOANTEST".into()],
        );
        if let Ok(loan_scheme_id) = &loan_scheme_id {
            client.await_n_confirmations(loan_scheme_id, 1)?;
        }

        let default_loan_scheme = client.get_default_loan_scheme()?;

        println!("default_loan_scheme : {:?}", default_loan_scheme);
        let vault_id =
            client.call::<String>("createvault", &[new_address.clone().into(), "".into()])?;
        client.await_n_confirmations(&vault_id, 1)?;
        println!("Creating vault with id : {}", vault_id);

        let utxo = client.call::<String>(
            "utxostoaccount",
            &[json!({ new_address.clone(): "100@DFI" })],
        )?;
        println!("utxo : {}", utxo);
        client.await_n_confirmations(&utxo, 1)?;

        let oracle_id = client.create_oracle(&["DFI", "BTC", "TSLA", "GOOGL"], 1)?;
        println!("oracle_id : {}", oracle_id);
        client.set_collateral_tokens(&["DFI", "BTC"])?;
        client.await_n_confirmations(&oracle_id, 10)?; // Wait for active price to update

        let deposit_tx = client.call::<String>(
            "deposittovault",
            &[
                vault_id.clone().into(),
                new_address.clone().into(),
                "100@DFI".into(),
            ],
        )?;
        println!("deposit_tx : {}", deposit_tx);
        client.await_n_confirmations(&deposit_tx, 1)?;

        client.set_loan_tokens(&["TSLA", "GOOGL"])?;
        let take_loan_tx = client.call::<String>(
            "takeloan",
            &[json!({ "vaultId": vault_id, "amounts":"1@TSLA" })],
        )?;
        client.await_n_confirmations(&take_loan_tx, 8)?; // Wait for active price to update

        println!("take_loan_tx : {}", take_loan_tx);

        let vault = client.call::<VaultInfo>("getvault", &[vault_id.clone().into()])?;
        println!("vault : {:?}", vault);

        // Cleanup section
        client.call::<String>("removeoracle", &[oracle_id.into()])?;
        client.create_token("dUSD")?;
        let mint_token_tx =
            client.call::<String>("minttokens", &["1000@dUSD".into(), "1@TSLA".into()])?;
        client.await_n_confirmations(&mint_token_tx, 1)?;

        let pool_address = client.get_new_address()?;
        println!("pool_address : {}", pool_address);
        client.add_pool_liquidity(&pool_address, ("TSLA", "dUSD"), (1, 300))?;
        let pool_pair_tx = client.create_pool_pair(("TSLA", "dUSD"));
        println!("pool_pair_tx : {:?}", pool_pair_tx);
        if let Ok(pool_pair_tx) = pool_pair_tx {
            client.await_n_confirmations(&pool_pair_tx, 1)?;
        } // discard error raised when pool_pair already exists.

        // Needs to create dUSD-DFI poolpair to burn DFI via SwapToDFIOverUSD.
        let pool_pair_tx = client.create_pool_pair(("dUSD", "DFI"));
        println!("pool_pair_tx : {:?}", pool_pair_tx);
        if let Ok(pool_pair_tx) = pool_pair_tx {
            client.await_n_confirmations(&pool_pair_tx, 1)?;
        } // discard error raised when pool_pair already exists.
        client.add_pool_liquidity(&pool_address, ("dUSD", "DFI"), (300, 100))?;
        client.await_n_confirmations(&take_loan_tx, 50)?;
        let loanpayback = client.call::<String>(
            "loanpayback",
            &[json!({
                "vaultId": vault_id.clone(),
                "from": new_address.clone(),
                "amounts": vault.loan_amount[0]
            })],
        )?;
        println!("loanpayback txid : {}", loanpayback);

        let close_vault_tx =
            client.call::<String>("closevault", &[vault_id.into(), new_address.clone().into()])?;
        if let Ok(loan_scheme_id) = loan_scheme_id {
            client.call::<String>("destroyloanscheme", &[loan_scheme_id.into()])?;
        }
        client.await_n_confirmations(&close_vault_tx, 1)?;
        Ok(())
    }
}
