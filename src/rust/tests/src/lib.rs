#[cfg(test)]
mod test {
    use anyhow::Result;
    use defi_rpc::Client;
    use serde_json::json;

    fn create_pool_pairs(symbol: (&str, &str), amount: (u32, u32)) -> Result<()> {
        let client = Client::from_env()?;
        let new_address = client.get_new_address()?;

        if symbol.0 != "DFI" {
            client.create_token(symbol.0)?;
            let mint_token_tx = client
                .call::<String>("minttokens", &[format!("{}@{}", amount.0, symbol.0).into()])?;
            client.await_n_confirmations(&mint_token_tx, 1)?;
        } else {
            let utxo = client.utxo_to_account(&new_address, &format!("{}@DFI", amount.0))?;
            client.await_n_confirmations(&utxo, 1)?;
        }

        if symbol.1 != "DFI" {
            client.create_token(symbol.1)?;
            let mint_token_tx = client.mint_tokens(amount.1, symbol.1)?;
            client.await_n_confirmations(&mint_token_tx, 1)?;
        } else {
            let utxo = client.utxo_to_account(&new_address, &format!("{}@DFI", amount.1))?;
            client.await_n_confirmations(&utxo, 1)?;
        }

        client.create_pool_pair(symbol)?;
        let add_liquidity_tx = client.add_pool_liquidity(&new_address, symbol, amount)?;
        client.await_n_confirmations(&add_liquidity_tx, 1)?;
        println!("Succesfully created {}-{} pool pair", symbol.0, symbol.1);
        Ok(())
    }

    fn setup(client: &Client) -> Result<()> {
        client.wait_for_balance_gte(500.)?;

        let oracle_id = client.create_oracle(&["DFI", "BTC", "TSLA", "GOOGL"], 1)?;
        println!("oracle_id : {}", oracle_id);
        client.set_collateral_tokens(&["DFI", "BTC"])?;
        client.await_n_confirmations(&oracle_id, 10)?; // Wait for active price to update

        client.set_loan_tokens(&["TSLA", "GOOGL"])?; // Prepare loan tokens

        let new_address = client.get_new_address()?;

        let utxo = client.utxo_to_account(&new_address, "500@DFI")?;
        client.await_n_confirmations(&utxo, 1)?;

        create_pool_pairs(("dUSD", "DFI"), (200, 200))?;
        create_pool_pairs(("dUSD", "TSLA"), (100, 10))?;

        let loan_scheme_id = client.call::<String>(
            "createloanscheme",
            &[120.into(), 5.into(), "LOANTEST".into()],
        );
        if let Ok(loan_scheme_id) = &loan_scheme_id {
            client.await_n_confirmations(loan_scheme_id, 1)?;
        } // Discard error if loan scheme already exists

        Ok(())
    }

    #[test]
    fn loan_payback_scenario() -> Result<()> {
        let client = Client::from_env()?;
        if client.network == "regtest" {
            // Setup only for regtest
            match setup(&client) {
                Ok(()) => println!(" ------- Succesful setup ------- "),
                Err(e) => panic!("Could not finish setup. Failed on : {:#?}", e),
            };
        }

        let default_loan_scheme = client.get_default_loan_scheme()?;
        println!("Using default loan scheme : {:#?}", default_loan_scheme);
        let new_address = client.get_new_address()?;
        println!("Generating new_address : {}", new_address);
        let vault_id = client.create_vault(&new_address, "")?;
        client.await_n_confirmations(&vault_id, 1)?;
        println!("Creating vault with id : {}", vault_id);

        let utxo_tx = client.utxo_to_account(&new_address, "100@DFI")?;
        println!("utxo_tx : {}", utxo_tx);
        client.await_n_confirmations(&utxo_tx, 1)?;

        let deposit_tx = client.deposit_to_vault(&vault_id, &new_address, "100@DFI")?;
        println!("deposit_tx : {}", deposit_tx);
        client.await_n_confirmations(&deposit_tx, 1)?;

        let take_loan_tx = client.take_loan(&vault_id, "1@TSLA")?;
        client.await_n_confirmations(&take_loan_tx, 8)?; // Wait for active price to update

        println!("take_loan_tx : {}", take_loan_tx);

        let send_token_tx = client.call::<String>(
            "sendtokenstoaddress",
            &[json!({}), json!({new_address.clone(): "10@TSLA"})],
        )?;
        client.await_n_confirmations(&send_token_tx, 1)?;

        let vault = client.get_vault(&vault_id)?;
        println!("vault : {:?}", vault);
        // Cleanup section
        // let loanpayback = client.loan_payback(&vault_id, &new_address, &vault.loan_amount[0])?;
        // println!("loanpayback txid : {}", loanpayback);

        let close_vault_tx =
            client.call::<String>("closevault", &[vault_id.into(), new_address.clone().into()])?;
        client.await_n_confirmations(&close_vault_tx, 1)?;
        Ok(())
    }
}
