use std::{result::Result as StdResult, str::FromStr, sync::Arc};

use ain_dftx::{deserialize, DfTx, Stack, COIN};
use ain_macros::ocean_endpoint;
use axum::{
    extract::{Json, Path},
    routing::{get, post},
    Extension, Router,
};
use bitcoin::{Transaction, Txid};
use defichain_rpc::{PoolPairRPC, RpcApi};
use rust_decimal::prelude::ToPrimitive;
use serde::{Deserialize, Serialize, Serializer};
use snafu::location;

use super::{query::Query, response::Response, AppContext};
use crate::{
    error::{ApiError, NotFoundKind},
    model::{default_max_fee_rate, MempoolAcceptResult, RawTransactionResult, RawTxDto},
    Error, Result,
};

enum TransactionResponse {
    HexString(String),
    TransactionDetails(Box<RawTransactionResult>),
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
    validate_composite_swap_tx(&ctx, &raw_tx_dto.hex).await?;

    let max_fee = match raw_tx_dto.max_fee_rate {
        Some(fee_rate) => {
            let fee_in_satoshis = fee_rate.checked_mul(COIN.into());
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
            if e.to_string().contains("TX decode failed") {
                Err(Error::BadRequest {
                    msg: "Transaction decode failed".to_string(),
                })
            } else {
                Err(Error::RpcError {
                    error: e,
                    location: location!(),
                })
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
            let fee_in_satoshis = fee_rate.checked_mul(COIN.into());
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
            if e.to_string().contains("TX decode failed") {
                Err(Error::BadRequest {
                    msg: "Transaction decode failed".to_string(),
                })
            } else {
                Err(Error::RpcError {
                    error: e,
                    location: location!(),
                })
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
            Self::HexString(ref s) => serializer.serialize_str(s),
            Self::TransactionDetails(ref details) => details.serialize(serializer),
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
                Error::NotFound { kind: NotFoundKind::RawTx }
            } else {
                Error::RpcError { error: e, location: location!() }
            }
        })?;
        Ok(TransactionResponse::HexString(tx_hex))
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
        Ok(TransactionResponse::TransactionDetails(Box::new(result)))
    }
}

async fn validate_composite_swap_tx(ctx: &Arc<AppContext>, hex: &String) -> Result<()> {
    if !hex.starts_with("040000000001") {
        return Ok(());
    }
    let data = hex::decode(hex)?;
    let tx = deserialize::<Transaction>(&data)?;

    let bytes = tx.output[0].script_pubkey.as_bytes();
    if bytes.len() <= 6 || bytes[0] != 0x6a || bytes[1] > 0x4e {
        return Ok(());
    }

    let offset = 1 + match bytes[1] {
        0x4c => 2,
        0x4d => 3,
        0x4e => 4,
        _ => 1,
    };
    let raw_tx = &bytes[offset..];
    let dftx = match deserialize::<Stack>(raw_tx) {
        Err(bitcoin::consensus::encode::Error::ParseFailed("Invalid marker")) => None,
        Err(e) => return Err(e.into()),
        Ok(Stack { dftx, .. }) => Some(dftx),
    };

    let Some(dftx) = dftx else { return Ok(()) };

    if let DfTx::CompositeSwap(swap) = dftx {
        let Some(last_pool_id) = swap.pools.iter().last() else {
            return Ok(());
        };

        let pool_pair = ctx
            .client
            .get_pool_pair(last_pool_id.to_string(), Some(true))
            .await?;

        let to_token_id = swap.pool_swap.to_token_id.0.to_string();

        for (_, info) in pool_pair.0 {
            if info.id_token_a == to_token_id || info.id_token_b == to_token_id {
                return Ok(());
            }
        }

        return Err(Error::BadRequest {
            msg: "Transaction is not a composite swap".to_string(),
        });
    };

    Ok(())
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new()
        .route("/send", post(send_raw_tx))
        .route("/test", post(test_raw_tx))
        .route("/:txid", get(get_raw_tx))
        .layer(Extension(ctx))
}
