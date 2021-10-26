use anyhow::Result;
use defi_rpc::Client;
use structopt::StructOpt;

static DEFAULT_COLLATERAL_TOKENS: [(&str, f32); 2] = [("DFI", 4.), ("BTC", 843.03)];

#[derive(Debug, StructOpt)]
#[structopt(
    about = "Create a new collateral token, the required oracle to track token/USD price and the associated dUSD poolpair. Requires foundation auth."
)]
pub struct CreateCollateralTokenCmd {
    /// Creates all default token
    #[structopt(short, long)]
    all: bool,

    /// Token name
    #[structopt(short, long, required_unless("all"))]
    token: Option<String>,

    /// Token value
    #[structopt(short, long, required_unless("all"))]
    value: Option<f32>,
}

impl CreateCollateralTokenCmd {
    fn create_collateral_token(&self, client: &Client, token: &str, value: f32) -> Result<()> {
        client.create_oracle(token, value)?;
        println!("Appointed oracle {}/USD.", token);

        client.set_collateral_tokens(&[&token])?;
        println!("Created loan token {}.", token);
        client.create_pool_pair((&token, "dUSD"))?;
        println!("Created poolpair {}-dUSD.", token);
        let collateral_token = client.get_token(&token)?;
        println!("token {} : {:#?}", token, collateral_token);
        Ok(())
    }

    pub fn run(&self, client: &Client) -> Result<()> {
        if self.all {
            for (token, value) in DEFAULT_COLLATERAL_TOKENS {
                self.create_collateral_token(client, token, value)?;
            }
        } else {
            self.create_collateral_token(
                client,
                self.token.as_ref().unwrap(),
                self.value.unwrap(),
            )?;
        }
        Ok(())
    }
}

static DEFAULT_LOAN_TOKENS: [(&str, f32); 14] = [
    ("GOOG", 2833.5),
    ("TSLA", 843.03),
    ("AMD", 112.12),
    ("AAPL", 144.84),
    ("NVDA", 218.62),
    ("CSCO", 55.25),
    ("EBAY", 74.9),
    ("MSFT", 304.21),
    ("NFLX", 628.29),
    ("PYPL", 268.35),
    ("TXN", 194.45),
    ("QCOM", 130.2),
    ("EA", 134.75),
    ("ADBE", 610.09),
];

#[derive(Debug, StructOpt)]
#[structopt(
    about = "Create a new loan token, the required oracle to track token/USD price and the associated dUSD poolpair. Requires foundation auth."
)]
pub struct CreateLoanTokenCmd {
    /// Creates all default token
    #[structopt(short, long)]
    all: bool,

    /// Token name
    #[structopt(short, long, required_unless("all"))]
    token: Option<String>,

    /// Token value
    #[structopt(short, long, required_unless("all"))]
    value: Option<f32>,
}

impl CreateLoanTokenCmd {
    fn create_loan_token(&self, client: &Client, token: &str, value: f32) -> Result<()> {
        if let Ok(_) = client.get_token(&token) {
            println!("Token {} already exists", token);
            return Ok(());
        }

        client.create_oracle(token, value)?;
        println!("Appointed oracle {}/USD.", token);

        client.set_loan_tokens(&[&token])?;
        println!("Created loan token {}.", token);
        client.create_pool_pair((&token, "dUSD"))?;
        println!("Created poolpair {}-dUSD.", token);
        let loan_token = client.get_token(&token)?;
        println!("token {} : {:#?}", token, loan_token);
        Ok(())
    }

    pub fn run(&self, client: &Client) -> Result<()> {
        if let Err(_) = client.get_token("dUSD") {
            client.create_token("dUSD")?;
            client.create_oracle("dUSD", 1.)?;
        }
        if self.all {
            for (token, value) in DEFAULT_LOAN_TOKENS {
                self.create_loan_token(client, token, value)?;
            }
        } else {
            self.create_loan_token(client, self.token.as_ref().unwrap(), self.value.unwrap())?;
        }
        Ok(())
    }
}
