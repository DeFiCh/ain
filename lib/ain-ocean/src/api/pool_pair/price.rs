use std::{collections::HashMap, sync::Arc};

use defichain_rpc::json::token::TokenInfo;
use serde::Serialize;

use super::{path::get_best_path, AppContext};
use crate::{
    api::{
        cache::{get_token_cached, list_token_cached},
        common::parse_display_symbol,
    },
    error::{Error, NotFoundKind},
    Result, TokenIdentifier,
};

#[derive(Clone, Debug, Serialize, Eq, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct DexPrice {
    pub token: TokenIdentifier,
    pub denomination_price: String,
}

#[derive(Clone, Debug, Serialize, Eq, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct DexPriceResponse {
    pub denomination: TokenIdentifier,
    pub dex_prices: HashMap<String, DexPrice>,
}

fn is_untradable_token(token: &TokenInfo) -> bool {
    token.is_lps || !token.is_dat || token.symbol == *"BURN" || !token.tradeable
}

pub async fn list_dex_prices(ctx: &Arc<AppContext>, symbol: String) -> Result<DexPriceResponse> {
    let (denomination_token_id, denomination_token_info) = get_token_cached(ctx, &symbol)
        .await?
        .ok_or(Error::NotFound(NotFoundKind::Token))?;

    if is_untradable_token(&denomination_token_info) {
        return Err(Error::UntradeableTokenError(denomination_token_info.symbol));
    };

    let tokens = list_token_cached(ctx)
        .await?
        .0
        .into_iter()
        .filter(|(_, info)| !is_untradable_token(info))
        .collect::<Vec<_>>();

    let mut dex_prices = HashMap::<String, DexPrice>::new();

    // For every token available, compute estimated return in denomination token
    for (id, info) in tokens {
        if id == denomination_token_id {
            continue;
        }
        let best_path = get_best_path(ctx, &id, &denomination_token_id).await?;

        dex_prices.insert(
            info.clone().symbol,
            DexPrice {
                token: TokenIdentifier {
                    id,
                    display_symbol: parse_display_symbol(&info),
                    name: info.name,
                    symbol: info.symbol,
                },
                denomination_price: best_path.estimated_return,
            },
        );
    }

    Ok(DexPriceResponse {
        denomination: TokenIdentifier {
            id: denomination_token_id,
            display_symbol: parse_display_symbol(&denomination_token_info),
            name: denomination_token_info.name,
            symbol: denomination_token_info.symbol,
        },
        dex_prices,
    })
}
