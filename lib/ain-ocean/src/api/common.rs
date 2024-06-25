use std::str::FromStr;
use bitcoin::{Address, Network, ScriptBuf};
use defichain_rpc::json::token::TokenInfo;
use rust_decimal::Decimal;
use rust_decimal_macros::dec;

use crate::hex_encoder::as_sha256;

use super::query::PaginationQuery;

pub fn parse_display_symbol(token_info: &TokenInfo) -> String {
    if token_info.is_lps {
        let tokens: Vec<&str> = token_info.symbol.split('-').collect();
        if tokens.len() == 2 {
            return format!(
                "{}-{}",
                parse_dat_symbol(tokens[0]),
                parse_dat_symbol(tokens[1])
            );
        }
    } else if token_info.is_dat {
        return parse_dat_symbol(&token_info.symbol);
    }

    token_info.symbol.clone()
}

pub fn parse_dat_symbol(symbol: &str) -> String {
    let special_symbols = ["DUSD", "DFI", "csETH"];

    if special_symbols.contains(&symbol) {
        symbol.to_string()
    } else {
        format!("d{}", symbol)
    }
}

pub fn format_number(v: Decimal) -> String {
    if v == dec!(0) {
        "0".to_string()
    } else {
        format!("{:.8}", v)
    }
}

pub fn from_script(script: ScriptBuf, network: Network) -> crate::Result<String> {
    let script = script.as_script();
    let address = Address::from_script(script, network)?.to_string();
    Ok(address)
}

pub fn to_script(address: &str, network: Network) -> crate::Result<ScriptBuf> {
    let addr = Address::from_str(address)?.require_network(network)?;
    Ok(ScriptBuf::from(addr))
}

pub fn address_to_hid(address: &str, network: Network) -> crate::Result<String> {
    let script = to_script(address, network)?;
    let bytes = script.to_bytes();
    Ok(as_sha256(bytes))
}

/// Finds the balance of a specified token symbol within a list of token strings.
///
/// This function iterates through a vector of token strings, where each string
/// represents an amount followed by a token symbol in the format "amount@symbol".
/// It searches for the specified token symbol and returns the corresponding balance.
/// If the token symbol is not found or if there are any parsing errors, it returns 0.
///
/// # Arguments
///
/// * `tokens` - A vector of strings representing token amounts with their symbols.
/// * `symbol` - A reference to a string representing the token symbol to find the balance for.
///
/// # Examples
///
/// ```
/// use rust_decimal::Decimal;
/// use rust_decimal_macros::dec;
/// use ain_ocean::api::{common::find_token_balance};
///
/// let tokens = vec![
///     "557.35080849@DFI".to_string(),
///     "9.98000000@BTC".to_string(),
///     "421.46947098@DUSD".to_string()
/// ];
/// let balance = find_token_balance(tokens, "DFI");
/// assert_eq!(balance, dec!(557.35080849));
/// ```
///
/// # Returns
///
/// The balance of the specified token symbol if found; otherwise, returns 0.
pub fn find_token_balance(tokens: Vec<String>, symbol: &str) -> Decimal {
    tokens
        .iter()
        .find_map(|t| {
            t.ends_with(symbol)
                .then(|| t.split('@').next().and_then(|v| v.parse::<Decimal>().ok()))
                .flatten()
        })
        .unwrap_or_default()
}

/// Provides a simulated pagination mechanism for iterators where native pagination is not available.
///
/// This trait extends any Rust iterator to include a `paginate` method, allowing for pseudo-pagination
/// based on custom logic. It's should only be used to query defid list* RPC that don't implement native pagination
///
/// # Warning
///
/// This method should be used cautiously, as it involves retrieving all data from the data source
/// before applying pagination. This can lead to significant performance and resource usage issues,
/// especially with large datasets. It is recommended to use this approach only defid does not accept
/// any pagination parameter.
///
/// # Parameters
///
/// - `query`: A reference to `PaginationQuery`
/// - `skip_while`: A closure that determines the starting point of data to consider, mimicking the
///   'start' parameter in traditional pagination. Once an item fails this condition, pagination starts.
///
/// # Example
///
/// This example is illustrative only and should not be executed directly as it involves API calls.
///
/// ```rust,ignore
/// use ain_ocean::api::common::Paginate;
///
/// let query = PaginationQuery {
///     next: Some(1)
///     limit: 3
/// };
///
/// let skip_while = |el: &LoanSchemeResult| match &query.next {
///     None => false,
///     Some(v) => v != &el.id,
/// };

/// let res: Vec<_> = ctx
///     .client
///     .list_loan_schemes()
///     .await?
///     .into_iter()
///     .fake_paginate(&query, skip_while)
///     .collect();
///
/// assert!(res.len() <= query.size, "The result should not contain more items than the specified limit");
/// assert!(res[0].id > query.next.unwrap(), "The result should start after the requested start id");
/// ```
pub trait Paginate<'a, T>: Iterator<Item = T> + Sized {
    fn fake_paginate<F>(
        self,
        query: &PaginationQuery,
        skip_while: F,
    ) -> Box<dyn Iterator<Item = T> + 'a>
    where
        F: FnMut(&T) -> bool + 'a;
    fn paginate(self, query: &PaginationQuery) -> Box<dyn Iterator<Item = T> + 'a>;
}

impl<'a, T, I> Paginate<'a, T> for I
where
    I: Iterator<Item = T> + 'a,
{
    fn fake_paginate<F>(
        self,
        query: &PaginationQuery,
        skip_while: F,
    ) -> Box<dyn Iterator<Item = T> + 'a>
    where
        F: FnMut(&T) -> bool + 'a,
    {
        Box::new(
            self.skip_while(skip_while)
                .skip(query.next.is_some() as usize)
                .take(query.size),
        )
    }
    fn paginate(self, query: &PaginationQuery) -> Box<dyn Iterator<Item = T> + 'a> {
        Box::new(self.skip(query.next.is_some() as usize).take(query.size))
    }
}

pub fn split_key(key: &str) -> Result<(String, String), String> {
    let parts: Vec<&str> = key.split('-').collect();
    if parts.len() == 2 {
        Ok((parts[0].to_owned(), parts[1].to_owned()))
    } else {
        Err(format!(
            "Invalid key format: '{}'. Expected format 'token-currency'.",
            key
        ))
    }
}
