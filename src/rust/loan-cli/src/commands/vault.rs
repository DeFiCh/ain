use anyhow::Result;
use defi_rpc::Client;
use structopt::StructOpt;

#[derive(Debug, StructOpt)]
#[structopt(about = "Create n vault for each available loan schemes.")]
pub struct CreateVaultCmd {
    /// Number of vault to create
    #[structopt(short, long, default_value = "1")]
    number: u32,
}

impl CreateVaultCmd {
    pub fn run(&self, client: &Client) -> Result<()> {
        let list_loan_schemes = client.list_loan_schemes()?;
        if list_loan_schemes.len() == 0 {
            return Ok(eprintln!("No available loan schemes."));
        }

        let owner_address = client.get_new_address()?;
        println!("Creating vaut for address : {}", owner_address);
        for loan_scheme in list_loan_schemes {
            for _ in 0..self.number {
                let tx = client.create_vault(&owner_address, &loan_scheme.id)?;
                println!("tx : {}", tx);
                client.await_n_confirmations(&tx, 1)?;
            }
        }
        let list_vaults = client.list_vaults(Some(&owner_address))?;

        println!(
            "Vaults created for address {} : {:#?}",
            owner_address, list_vaults
        );

        Ok(())
    }
}
