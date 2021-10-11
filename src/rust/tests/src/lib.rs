#[cfg(test)]
mod test {
    use anyhow::Result;
    use defi_rpc::Client;
    use serde_json::json;

    #[test]
    fn calculate_loan_interest() -> Result<()> {
        let client = Client::from_env()?;
        let new_address = client.call::<String>("getnewaddress", &[])?;

        let default_loan_scheme = client.get_default_loan_scheme()?;

        println!("default_loan_scheme : {:?}", default_loan_scheme);
        let vault_id =
            client.call::<String>("createvault", &[new_address.clone().into(), "".into()])?;
        client.await_n_confirmations(&vault_id, 1)?;
        println!("vault_id : {}", vault_id);

        let utxo = client.call::<String>(
            "utxostoaccount",
            &[json!({new_address.clone(): "1000@DFI"})],
        )?;
        println!("utxo : {}", utxo);
        client.await_n_confirmations(&utxo, 1)?;

        let oracle_id = client.create_oracle(&["DFI", "BTC", "TSLA", "GOOGL"], 10)?;

        let deposit_tx = client.call::<String>(
            "deposittovault",
            &[
                vault_id.clone().into(),
                new_address.into(),
                "200@DFI".into(),
            ],
        )?;
        client.await_n_confirmations(&deposit_tx, 1)?;

        let take_loan_tx = client.call::<String>(
            "takeloan",
            &[json!({ "vaultId": vault_id, "amounts":"1@TSLA" })],
        )?;
        client.await_n_confirmations(&take_loan_tx, 1)?;

        // Cleanup section
        client.call::<String>("removeoracle", &[oracle_id.into()])?;
        client.call::<String>("closevault", &[vault_id.into()])?;

        Ok(())
    }
}
