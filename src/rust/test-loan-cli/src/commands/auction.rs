use anyhow::Result;
use defi_rpc::Client;
use structopt::StructOpt;

#[derive(Debug, StructOpt)]
#[structopt(
    about = "Create and liquidate n vaults to create resulting auctions. Requires foundation auth."
)]
pub struct CreateAuctionCmd {
    /// Number of auctions to create.
    number: u32,
}

impl CreateAuctionCmd {
    pub fn run(&self, client: &Client) -> Result<()> {
        let default_loan_scheme = client.get_default_loan_scheme()?;
        let token = "TEST";
        let oracle_id = client.create_oracle(&[token], 100)?;
        client.await_n_confirmations(&oracle_id, 1)?;
        client.set_loan_tokens(&[token])?;
        client.create_pool_pair((token, "dUSD"))?;
        // for i in 1..self.number {
        let address = client.get_new_address()?;
        let vault_id = client.create_vault(&address, &default_loan_scheme.id)?;
        client.await_n_confirmations(&vault_id, 1)?;
        let utxo_tx = client.utxo_to_account(&address, "100@DFI")?;
        client.await_n_confirmations(&utxo_tx, 1)?;
        let deposit_tx = client.deposit_to_vault(&vault_id, &address, "100@DFI")?;
        client.await_n_confirmations(&deposit_tx, 1)?;
        let take_loan_tx = client.take_loan(&vault_id, &format!("1@{}", token))?;
        client.await_n_confirmations(&take_loan_tx, 1)?;
        let vault = client.get_vault(&vault_id)?;
        println!("vault : {:?}", vault);
        // }

        let set_oracle_tx = client.set_oracle_data(&oracle_id, "TEST", 1)?;
        client.await_n_confirmations(&set_oracle_tx, 1)?;
        let vault = client.get_vault(&vault_id)?;
        println!("vault : {:?}", vault);

        Ok(())
    }
}
