use defichain_rpc::json::token::TokenInfo;

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
