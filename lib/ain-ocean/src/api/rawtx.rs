use std::{str::FromStr, sync::Arc};

use ain_macros::ocean_endpoint;
use axum::{
    extract::{Json, Path},
    routing::{get, post},
    Extension, Router,
};
use bitcoin::Txid;
use defichain_rpc::RpcApi;
use rust_decimal::prelude::ToPrimitive;
use rust_decimal_macros::dec;
use serde::Deserialize;

use super::{query::Query, response::Response, AppContext};
use crate::{
    error::ApiError,
    model::{default_max_fee_rate, MempoolAcceptResult, RawTransactionResult, RawTxDto},
    Error, Result,
};

#[ocean_endpoint]
async fn send_rawtx(
    Extension(ctx): Extension<Arc<AppContext>>,
    Json(raw_tx_dto): Json<RawTxDto>,
) -> Result<String> {
    validate(raw_tx_dto.hex.clone())?;
    let max_fee = match raw_tx_dto.max_fee_rate {
        Some(fee_rate) => {
            let sat_per_bitcoin = dec!(100_000_000);
            let fee_in_satoshis = fee_rate.checked_mul(sat_per_bitcoin);
            match fee_in_satoshis {
                Some(value) => Some(value.to_u64().unwrap_or_default()),
                None => Some(default_max_fee_rate().to_sat()),
            }
        }
        None => Some(default_max_fee_rate().to_sat()),
    };
    match ctx
        .client
        .send_raw_transaction(raw_tx_dto.hex, max_fee)
        .await
    {
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
    let max_fee = match raw_tx_dto.max_fee_rate {
        Some(fee_rate) => {
            let sat_per_bitcoin = dec!(100_000_000);
            let fee_in_satoshis = fee_rate.checked_mul(sat_per_bitcoin);
            match fee_in_satoshis {
                Some(value) => Some(value.to_u64().unwrap_or_default()),
                None => Some(default_max_fee_rate().to_sat()),
            }
        }
        None => Some(default_max_fee_rate().to_sat()),
    };
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

#[derive(Deserialize, Default)]
struct QueryParams {
    verbose: bool,
}
#[ocean_endpoint]
async fn get_raw_tx(
    Path(txid): Path<String>,
    Query(QueryParams { verbose }): Query<QueryParams>,
    Extension(ctx): Extension<Arc<AppContext>>,
) -> Result<(String, Option<RawTransactionResult>)> {
    let tx_hash = Txid::from_str(&txid)?;
    if !verbose {
        let tx_hex = ctx.client.get_raw_transaction_hex(&tx_hash, None).await?;
        Ok((txid, None))
    } else {
        let tx_info = ctx.client.get_raw_transaction_info(&tx_hash, None).await?;
        let result = RawTransactionResult {
            in_active_chain: tx_info.in_active_chain,
            hex: tx_info.hex,
            txid: tx_info.txid,
            hash: tx_info.hash,
            size: tx_info.size,
            vsize: tx_info.vsize,
            version: tx_info.version,
            locktime: tx_info.locktime,
            vin: tx_info.vin,
            vout: tx_info.vout,
            blockhash: tx_info.blockhash,
            confirmations: tx_info.confirmations,
            time: tx_info.time,
            blocktime: tx_info.blocktime,
        };
        Ok((txid, Some(result))) // Correctly wrap in a tuple and Some
    }
}

fn validate(hex: String) -> Result<()> {
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
