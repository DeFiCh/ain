use anyhow::Result;
use defi_rpc::Client;
use structopt::StructOpt;

#[derive(Debug, StructOpt)]
#[structopt(about = "Create loan scheme. Requires foundation auth.")]
pub struct CreateLoanSchemeCmd {
    #[structopt(short, long)]
    /// Optional number of loan schemes to create.
    /// If passed, will create n random loan schemes such as 110-10 L11010, 120-5 L1205, etc.
    number: Option<u32>,

    /// Loan scheme mininmum collateral ratio.
    #[structopt(required_unless = "number")]
    ratio: Option<u32>,

    /// Loan scheme interest rate.
    #[structopt(required_unless = "number")]
    interest: Option<String>,

    /// Loan scheme id
    #[structopt(required_unless = "number")]
    id: Option<String>,
}

impl CreateLoanSchemeCmd {
    pub fn run(&self, client: &Client) -> Result<()> {
        if let Some(n) = self.number {
            for i in 1..n {
                let val = i * 10;
                let min_col_ratio = 100 + val;
                let interest_rate = format!("{:.1}", 100f32 / val as f32);
                let id = format!("L{}{}", min_col_ratio, interest_rate);
                match client.create_loan_scheme(min_col_ratio, &interest_rate, &id) {
                    Ok(tx) => {
                        println!(
                            "Created loan scheme {} with collateral ratio : {} and interest rate : {:?}",
                            id, min_col_ratio, interest_rate
                        );
                        println!("Txid : {}", tx);
                    }
                    Err(e) => eprintln!("{}", e),
                }
            }
        } else {
            let min_col_ratio = self.ratio.expect("Error getting minimum collateral ratio"); // Should never happen with structopt's required_unless.
            let interest_rate = self.interest.as_ref().expect("Error getting interest rate");
            let id = self.id.as_ref().expect("Error getting loan scheme id");
            match client.create_loan_scheme(min_col_ratio, &interest_rate, &id) {
                Ok(tx) => {
                    println!(
                        "Created loan scheme {} with collateral ratio : {} and interest rate : {:?}",
                        id, min_col_ratio, interest_rate
                    );
                    println!("Txid : {}", tx);
                }
                Err(e) => eprintln!("{}", e),
            }
        }
        Ok(())
    }
}
