use std::{str::FromStr, sync::Arc};

use ain_macros::ocean_endpoint;
use axum::{
    extract::Path,
    response::Json,
    routing::{get, post},
    Extension, Router,
};
use bitcoin::{consensus::encode::deserialize, Transaction, Txid};
use defichain_rpc::{
    json::{Bip125Replaceable, GetTransactionResult, TestMempoolAcceptResult},
    RpcApi,
};
use rust_decimal::Decimal;
use rust_decimal_macros::dec;

use super::{response::Response, AppContext};
use crate::{
    api::response::ApiPagedResponse,
    error::ApiError,
    model::{RawTransaction, RawTxDto, TransctionDetails, WalletTxInfo},
    Result,
};
const DEFAULT_MAX_FEE_RATE: Decimal = dec!(0.1);

#[ocean_endpoint]
async fn send_rawtx(
    Path(tx): Path<RawTxDto>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<String>> {
    let mut tx = tx.clone();
    if tx.max_fee_rate.is_none() {
        tx.max_fee_rate = Some(DEFAULT_MAX_FEE_RATE);
    };
    let trx = defichain_rpc::RawTx::raw_hex(tx.hex);
    let tx_hash = ctx.client.send_raw_transaction(trx).await?;
    Ok(Response::new(tx_hash.to_string()))
}

#[ocean_endpoint]
async fn test_rawtx(
    Path(tx): Path<RawTxDto>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<Vec<TestMempoolAcceptResult>>> {
    let mut tx = tx.clone();
    if tx.max_fee_rate.is_none() {
        tx.max_fee_rate = Some(DEFAULT_MAX_FEE_RATE);
    };
    let trx = defichain_rpc::RawTx::raw_hex(tx.hex);
    let mempool_tx = ctx.client.test_mempool_accept(&[trx]).await?;
    Ok(Response::new(mempool_tx))
}
#[ocean_endpoint]
async fn get_raw_tx(
    Path(txid): Path<String>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<RawTransaction>> {
    format!("Details of raw transaction with txid {}", txid);
    let tx_hash = Txid::from_str(&txid)?;
    let tx_result = ctx.client.get_transaction(&tx_hash, Some(true)).await?;

    let details: Vec<TransctionDetails> = tx_result
        .details
        .into_iter()
        .map(|detail| {
            let address = match detail.address {
                Some(addr) => Some(addr),
                None => None,
            };

            TransctionDetails {
                address: address,
                category: detail.category.to_owned(),
                amount: detail.amount.to_sat(),
                label: detail.label,
                vout: detail.vout,
                fee: detail.fee.map(|f| f.to_sat()),
                abandoned: detail.abandoned,
                hex: hex::encode(&tx_result.hex),
                blockhash: tx_result.info.blockhash.clone(),
                confirmations: tx_result.info.confirmations,
                time: tx_result.info.time,
                blocktime: tx_result.info.blocktime,
            }
        })
        .collect();

    let bip_125 = match tx_result.info.bip125_replaceable {
        Bip125Replaceable::Yes => Some("Yes".to_string()),
        Bip125Replaceable::No => Some("No".to_string()),
        Bip125Replaceable::Unknown => Some("Unknown".to_string()),
    };

    let raw_tx = RawTransaction {
        info: WalletTxInfo {
            confirmations: tx_result.info.confirmations,
            blockhash: tx_result.info.blockhash,
            blockindex: tx_result.info.blockindex,
            blocktime: tx_result.info.blocktime,
            blockheight: tx_result.info.blockheight,
            txid: tx_result.info.txid,
            time: tx_result.info.time,
            timereceived: tx_result.info.timereceived,
            bip125_replaceable: bip_125,
            wallet_conflicts: tx_result.info.wallet_conflicts,
        },
        amount: tx_result.amount.to_sat(),
        fee: tx_result.fee.map(|amount| amount.to_sat()),
        details: details,
        hex: tx_result.hex,
    };
    Ok(Response::new(raw_tx))
}

async fn validate(hex: String) {
    if !hex.starts_with("040000000001") {
        return;
    }
    let buffer = hex::decode(hex).expect("Decoding failed");
    let transaction: Transaction = deserialize(&buffer).expect("Failed to deserialize transaction");
    if transaction.output.len() != 2 {
        return;
    }
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/send", post(send_rawtx))
        .route("/test", get(test_rawtx))
        .route("/:txid", get(get_raw_tx))
        .layer(Extension(ctx))
}
