use std::str::FromStr;

use ain_dftx::{Currency, Token};
use bitcoin::{Address, ScriptBuf, Txid};
use defichain_rpc::json::token::TokenInfo;
use rust_decimal::Decimal;
use rust_decimal_macros::dec;
use snafu::OptionExt;

use super::query::PaginationQuery;
use crate::{
    error::{
        Error::ToArrayError, InvalidAmountSnafu, InvalidFixedIntervalPriceSnafu,
        InvalidPriceTickerSortKeySnafu, InvalidPoolPairSymbolSnafu, InvalidTokenCurrencySnafu,
    },
    hex_encoder::as_sha256,
    model::PriceTickerId,
    network::Network,
    Result,
};

#[must_use]
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

#[must_use]
pub fn parse_dat_symbol(symbol: &str) -> String {
    let special_symbols = ["DUSD", "DFI", "csETH"];

    if special_symbols.contains(&symbol) {
        symbol.to_string()
    } else {
        format!("d{symbol}")
    }
}

pub fn parse_pool_pair_symbol(item: &str) -> Result<(String, String)> {
    let mut parts = item.split('-');
    let a = parts
        .next()
        .context(InvalidPoolPairSymbolSnafu { item })?
        .to_string();
    let b = parts
        .next()
        .context(InvalidPoolPairSymbolSnafu { item })?
        .to_string();

    Ok((a, b))
}

pub fn parse_token_currency(item: &str) -> Result<(Token, Currency)> {
    let mut parts = item.split('-');
    let token = parts
        .next()
        .context(InvalidTokenCurrencySnafu { item })?
        .to_string();
    let currency = parts
        .next()
        .context(InvalidTokenCurrencySnafu { item })?
        .to_string();

    Ok((token, currency))
}

pub fn parse_fixed_interval_price(item: &str) -> Result<(Token, Currency)> {
    let mut parts = item.split('/');
    let token = parts
        .next()
        .context(InvalidFixedIntervalPriceSnafu { item })?
        .to_string();
    let currency = parts
        .next()
        .context(InvalidFixedIntervalPriceSnafu { item })?
        .to_string();

    Ok((token, currency))
}

pub fn parse_amount(item: &str) -> Result<(String, String)> {
    let mut parts = item.split('@');
    let amount = parts
        .next()
        .context(InvalidAmountSnafu { item })?
        .to_string();
    let symbol = parts
        .next()
        .context(InvalidAmountSnafu { item })?
        .to_string();

    Ok((amount, symbol))
}

pub fn parse_query_height_txno(item: &str) -> Result<(u32, usize)> {
    let mut parts = item.split('-');
    let height = parts.next().context(InvalidAmountSnafu { item })?;
    let txno = parts.next().context(InvalidAmountSnafu { item })?;

    let height = height.parse::<u32>()?;
    let txno = txno.parse::<usize>()?;

    Ok((height, txno))
}

pub fn parse_query_height_txid(item: &str) -> Result<(u32, Txid)> {
    let mut parts = item.split('-');
    let encoded_height = parts.next().context(InvalidAmountSnafu { item })?;
    let txid = parts.next().context(InvalidAmountSnafu { item })?;

    let height_in_bytes: [u8; 4] = hex::decode(encoded_height)?
        .try_into()
        .map_err(|_| ToArrayError)?;
    let height = u32::from_be_bytes(height_in_bytes);
    let txid = txid.parse::<Txid>()?;

    Ok((height, txid))
}

pub fn parse_price_ticker_sort(item: &str) -> Result<PriceTickerId> {
    let mut parts = item.split('-');
    let count_height_token = parts.next().context(InvalidPriceTickerSortKeySnafu { item })?;
    let encoded_count = &count_height_token[..8];
    let encoded_height = &count_height_token[8..16];
    let token = &count_height_token[16..];
    let token = token.to_string();

    let count: [u8; 4] = hex::decode(encoded_count)?
        .try_into()
        .map_err(|_| ToArrayError)?;

    let height: [u8; 4] = hex::decode(encoded_height)?
        .try_into()
        .map_err(|_| ToArrayError)?;

    let currency = parts
        .next()
        .context(InvalidTokenCurrencySnafu { item })?
        .to_string();

    Ok((count, height, token, currency))
}

#[must_use]
pub fn format_number(v: Decimal) -> String {
    if v == dec!(0) {
        "0".to_string()
    } else {
        format!("{v:.8}")
    }
}

pub fn from_script(script: &ScriptBuf, network: Network) -> Result<String> {
    Ok(Address::from_script(script, network.into())?.to_string())
}

#[test]
fn test_from_script() {
    // OP_0 { type: 'OP_0', code: 0 },
    // OP_PUSHDATA {
    //   type: 'OP_PUSHDATA',
    //   hex: '05768f2d17f0016b5720bb49859cbb065041716f'
    // }
    let script = ScriptBuf::from_hex("001405768f2d17f0016b5720bb49859cbb065041716f").unwrap();
    let addr = from_script(&script, Network::Mainnet).unwrap();
    assert_eq!(
        addr,
        "df1qq4mg7tgh7qqkk4eqhdyct89mqegyzut0jjz8rg".to_string()
    )
}

pub fn to_script(address: &str, network: Network) -> Result<ScriptBuf> {
    let addr = Address::from_str(address)?.require_network(network.into())?;
    Ok(ScriptBuf::from(addr))
}

pub fn address_to_hid(address: &str, network: Network) -> Result<[u8; 32]> {
    let script = to_script(address, network)?;
    let bytes = script.to_bytes();
    Ok(as_sha256(&bytes))
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
#[must_use]
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
                .skip(usize::from(query.next.is_some()))
                .take(query.size),
        )
    }
    fn paginate(self, query: &PaginationQuery) -> Box<dyn Iterator<Item = T> + 'a> {
        Box::new(
            self.skip(usize::from(query.next.is_some()))
                .take(query.size),
        )
    }
}
