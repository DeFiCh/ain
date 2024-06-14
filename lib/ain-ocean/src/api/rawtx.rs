use std::{str::FromStr, sync::Arc};

use ain_macros::ocean_endpoint;
use axum::{
    extract::{Json, Path},
    routing::{get, post},
    Extension, Router,
};
use bitcoin::Txid;
use defichain_rpc::{json::Bip125Replaceable, RpcApi};
use rust_decimal::{prelude::ToPrimitive, Decimal};

use super::{response::Response, AppContext};
use crate::{
    error::ApiError,
    model::{
        default_max_fee_rate, MempoolAcceptResult, RawTransaction, RawTxDto, TransctionDetails,
        WalletTxInfo,
    },
    Error, Result,
};

#[ocean_endpoint]
async fn send_rawtx(
    Extension(ctx): Extension<Arc<AppContext>>,
    Json(raw_tx_dto): Json<RawTxDto>,
) -> Result<String> {
    println!("{:?}", raw_tx_dto.hex.clone());
    validate(raw_tx_dto.hex.clone())?;
    let mut max_fee = Some(default_max_fee_rate().unwrap().to_sat());
    if let Some(fee_rate) = raw_tx_dto.max_fee_rate {
        let sat_per_bitcoin = Decimal::new(100_000_000, 0);
        let fee_in_satoshis = fee_rate * sat_per_bitcoin;
        max_fee = fee_in_satoshis.round().to_u64();
    }
    let trx = defichain_rpc::RawTx::raw_hex(raw_tx_dto.hex);
    match ctx.client.send_raw_transaction(trx, max_fee).await {
        Ok(tx_hash) => Ok(tx_hash.to_string()),
        Err(e) => {
            eprintln!("Failed to send raw transaction: {:?}", e);
            Err(Error::RpcError(e))
        }
    }
}
#[ocean_endpoint]
async fn test_rawtx(
    Extension(ctx): Extension<Arc<AppContext>>,
    Json(raw_tx_dto): Json<RawTxDto>,
) -> Result<Response<Vec<MempoolAcceptResult>>> {
    let trx = defichain_rpc::RawTx::raw_hex(raw_tx_dto.hex);
    let mut max_fee = Some(default_max_fee_rate().unwrap().to_sat());
    if let Some(fee_rate) = raw_tx_dto.max_fee_rate {
        let sat_per_bitcoin = Decimal::new(100_000_000, 0);
        let fee_in_satoshis = fee_rate * sat_per_bitcoin;
        max_fee = fee_in_satoshis.round().to_u64();
    }
    match ctx.client.test_mempool_accept(&[trx], max_fee).await {
        Ok(mempool_tx) => {
            let results = mempool_tx
                .into_iter()
                .map(|tx_result| MempoolAcceptResult {
                    txid: tx_result.txid,
                    allowed: tx_result.allowed,
                    reject_reason: tx_result.reject_reason,
                    vsize: tx_result.vsize,
                    fees: tx_result.fees.map(|f| f.base),
                })
                .collect::<Vec<MempoolAcceptResult>>();
            Ok(Response::new(results))
        }
        Err(e) => {
            eprintln!("Failed to send raw transaction: {:?}", e);
            Err(Error::RpcError(e))
        }
    }
}

#[ocean_endpoint]
async fn get_raw_tx(
    Path((txid, verbose)): Path<(String, bool)>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<Response<RawTransaction>> {
    let tx_hash = Txid::from_str(&txid)?;
    let tx_result = ctx.client.get_transaction(&tx_hash, Some(verbose)).await?;
    let details: Vec<TransctionDetails> = tx_result
        .details
        .into_iter()
        .map(|detail| TransctionDetails {
            address: detail.address,
            category: detail.category.to_owned(),
            amount: detail.amount.to_sat(),
            label: detail.label,
            vout: detail.vout,
            fee: detail.fee.map(|f| f.to_sat()),
            abandoned: detail.abandoned,
            hex: hex::encode(&tx_result.hex),
            blockhash: tx_result.info.blockhash,
            confirmations: tx_result.info.confirmations,
            time: tx_result.info.time,
            blocktime: tx_result.info.blocktime,
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
        details,
        hex: tx_result.hex,
    };
    Ok(Response::new(raw_tx))
}

fn validate(hex: String) -> Result<()> {
    println!("{:?}", hex);
    if !hex.starts_with("040000000001") {
        return Err(Error::ValidationError(
            "Transaction does not start with the expected prefix.".to_string(),
        ));
    }
    Ok(())
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    println!("{:?}", ctx.network);
    Router::new()
        .route("/send", post(send_rawtx))
        .route("/test", post(test_rawtx))
        .route("/:txid", get(get_raw_tx))
        .layer(Extension(ctx))
}
