use defichain_rpc::json::token::TokenInfo;
use rust_decimal::Decimal;
use serde::{Deserialize, Serialize};

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

fn parse_dat_symbol(symbol: &str) -> String {
    let special_symbols = ["DUSD", "DFI", "csETH"];

    if special_symbols.contains(&symbol) {
        symbol.to_string()
    } else {
        format!("d{}", symbol)
    }
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
/// let tokens = vec![
///     "557.35080849@DFI".to_string(),
///     "9.98000000@BTC".to_string(),
///     "421.46947098@DUSD".to_string()
/// ];
/// let balance = find_token_balance(tokens, "DFI");
/// assert_eq!(balance, 557.35080849);
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
                .then(|| t.split("@").next().and_then(|v| v.parse::<Decimal>().ok()))
                .flatten()
        })
        .unwrap_or_default()
}
