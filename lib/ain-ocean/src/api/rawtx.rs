use std::{result::Result as StdResult, str::FromStr, sync::Arc};

use ain_dftx::{deserialize, DfTx};
use ain_macros::ocean_endpoint;
use axum::{
    extract::{Json, Path},
    routing::{get, post},
    Extension, Router,
};
use bitcoin::{Transaction, Txid};
use defichain_rpc::{PoolPairRPC, RpcApi};
use rust_decimal::prelude::ToPrimitive;
use rust_decimal_macros::dec;
use serde::{Deserialize, Serialize, Serializer};

use super::{query::Query, response::Response, AppContext};
use crate::{
    error::{ApiError, NotFoundKind},
    model::{default_max_fee_rate, MempoolAcceptResult, RawTransactionResult, RawTxDto},
    Error, Result,
};

enum TransactionResponse {
    HexString(String),
    TransactionDetails(RawTransactionResult),
}

#[derive(Deserialize, Default)]
struct QueryParams {
    verbose: bool,
}

#[ocean_endpoint]
async fn send_raw_tx(
    Extension(ctx): Extension<Arc<AppContext>>,
    Json(raw_tx_dto): Json<RawTxDto>,
) -> Result<String> {
    validate(ctx.clone(), raw_tx_dto.hex.clone()).await?;
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
            if e.to_string().contains("Transaction decode failed") {
                Err(Error::BadRequest("Transaction decode failed".to_string()))
            } else {
                Err(Error::RpcError(e))
            }
        }
    }
}
#[ocean_endpoint]
async fn test_raw_tx(
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
            if e.to_string().contains("TX decode failed") {
                Err(Error::BadRequest("Transaction decode failed".to_string()))
            } else {
                Err(Error::RpcError(e))
            }
        }
    }
}

impl Serialize for TransactionResponse {
    fn serialize<S>(&self, serializer: S) -> StdResult<S::Ok, S::Error>
    where
        S: Serializer,
    {
        match *self {
            TransactionResponse::HexString(ref s) => serializer.serialize_str(s),
            TransactionResponse::TransactionDetails(ref details) => details.serialize(serializer),
        }
    }
}

#[ocean_endpoint]
async fn get_raw_tx(
    Extension(ctx): Extension<Arc<AppContext>>,
    Path(txid): Path<String>,
    Query(QueryParams { verbose }): Query<QueryParams>,
) -> Result<TransactionResponse> {
    let tx_hash = Txid::from_str(&txid)?;
    if !verbose {
        let tx_hex = ctx.client.get_raw_transaction_hex(&tx_hash, None).await.map_err(|e| {
            if e.to_string().contains("No such mempool or blockchain transaction. Use gettransaction for wallet transactions.") {
                Error::NotFound(NotFoundKind::RawTx)
            } else {
                Error::RpcError(e)
            }
        })?;
        Ok(TransactionResponse::HexString(tx_hex))
    } else {
        let tx_info = ctx
            .client
            .get_raw_transaction_info(&tx_hash, None)
            .await
            .map_err(|e| {
                eprintln!("Failed to get raw transaction hex: {:?}", e);
                Error::RpcError(e)
            })?;
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
        Ok(TransactionResponse::TransactionDetails(result))
    }
}

async fn validate(ctx: Arc<AppContext>, hex: String) -> Result<()> {
    if !hex.starts_with("040000000001") {
        return Ok(());
    }
    let data = hex::decode(hex)?;
    println!("decode_hex {:?}", data);
    let trx = deserialize::<Transaction>(&data)?;
    let bytes = trx.output[0].clone().script_pubkey.into_bytes();
    let tx: Option<DfTx> = if bytes.len() > 2 && bytes[0] == 0x6a && bytes[1] <= 0x4e {
        let offset = 1 + match bytes[1] {
            0x4c => 2,
            0x4d => 3,
            0x4e => 4,
            _ => 1,
        };

        let raw_tx = &bytes[offset..];
        Some(deserialize::<DfTx>(raw_tx)?)
    } else {
        return Ok(());
    };

    if let Some(tx) = tx {
        if let DfTx::CompositeSwap(composite_swap) = tx {
            if composite_swap.pools.as_ref().is_empty() {
                return Ok(());
            }
            let pool_id = composite_swap.pools.iter().last().unwrap();
            let tokio_id = composite_swap.pool_swap.to_token_id.0.to_string();
            let pool_pair = ctx
                .client
                .get_pool_pair(pool_id.to_string(), Some(true))
                .await?;
            for (_, pool_pair_info) in pool_pair.0 {
                if pool_pair_info.id_token_a.eq(&tokio_id)
                    || pool_pair_info.id_token_b.eq(&tokio_id)
                {
                    println!("Found a match: {:?}", pool_pair_info);
                }
            }
            Ok(())
        } else {
            Err(Error::BadRequest(
                "Transaction is not a composite swap".to_string(),
            ))
        }
    } else {
        Ok(())
    }
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    println!("{:?}", ctx.network);
    Router::new()
        .route("/send", post(send_raw_tx))
        .route("/test", post(test_raw_tx))
        .route("/:txid", get(get_raw_tx))
        .layer(Extension(ctx))
}
