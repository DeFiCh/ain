use anyhow::Result;
use defi_rpc::Client;
use structopt::StructOpt;

#[derive(Debug, StructOpt)]
#[structopt(
    about = "Create a new loan token, the required oracle to track token/USD price and the associated dUSD poolpair. Requires foundation auth."
)]
pub struct CreateLoanTokenCmd {
    #[structopt(short, long)]
    token: String,
}

impl CreateLoanTokenCmd {
    pub fn run(&self, client: &Client) -> Result<()> {
        if let Ok(_) = client.get_token(&self.token) {
            println!("Token {} already exists", self.token);
            return Ok(());
        }

        client.create_oracle(&[&self.token], 100)?;
        println!("Appointed oracle {}/USD.", self.token);

        client.set_loan_tokens(&[&self.token])?;
        println!("Created loan token {}.", self.token);
        client.create_pool_pair((&self.token, "dUSD"))?;
        println!("Created poolpair {}-dUSD.", self.token);
        let token = client.get_token(&self.token)?;
        println!("token {} : {:#?}", self.token, token);
        Ok(())
    }
}
