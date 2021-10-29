use anyhow::Result;
use defi_rpc::Client;
use structopt::StructOpt;

#[derive(Debug, StructOpt)]
#[structopt(about = "Add liquidity to pool pair")]
pub struct AddPoolLiquidityCmd {
    token_a: String,
    token_b: String,
}

impl AddPoolLiquidityCmd {
    pub fn run(&self, client: &Client) -> Result<()> {
        let address = client.get_new_address()?;
        let list_prices = client
            .list_fixed_interval_prices()?
            .into_iter()
            .filter(|x| x.is_live)
            .collect::<Vec<_>>();

        let fixed_price_a = list_prices
            .iter()
            .find(|price| price.price_feed_id.contains(&self.token_a))
            .expect(&format!(
                "Token {} has no fixed interval price available",
                self.token_a,
            ));

        let fixed_price_b = list_prices
            .iter()
            .find(|price| price.price_feed_id.contains(&self.token_b))
            .expect(&format!(
                "Token {} has no fixed interval price available",
                self.token_b,
            ));

        let ratio = fixed_price_a.active_price / fixed_price_b.active_price;
        let amount_a = 10.;
        let amount_b = ratio * 10.;
        let add_liquidity_tx = client.add_pool_liquidity(
            &address,
            (&self.token_a, &self.token_b),
            (amount_a, amount_b),
        )?;
        client.await_n_confirmations(&add_liquidity_tx, 1)?;
        println!(
            "Added {} {} and {} {} to {}-{} poolpair !",
            amount_a, self.token_a, amount_b, self.token_b, self.token_a, self.token_b
        );
        Ok(())
    }
}
