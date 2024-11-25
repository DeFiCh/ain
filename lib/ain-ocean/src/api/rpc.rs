use std::sync::Arc;

use ain_macros::ocean_endpoint;
use axum::{routing::post, Extension, Json, Router};
use defichain_rpc::RpcApi;
use serde::{Deserialize, Serialize};

use super::{response::ApiRpcResponse, AppContext};
use crate::{
    error::{ApiError, Error},
    Result,
};

#[derive(Serialize, Deserialize, Default, Clone)]
#[serde(rename_all = "camelCase")]
struct RpcDto {
    method: String,
    params: Vec<serde_json::Value>,
}

fn method_whitelist(method: &str) -> Result<()> {
    let methods = [
        "getblockchaininfo",
        "getblockhash",
        "getblockcount",
        "getblock",
        "getblockstats",
        "getgov",
        "validateaddress",
        "listcommunitybalances",
        "getaccounthistory",
        "getfutureswapblock",
        "getpendingfutureswaps",
        "sendrawtransaction",
        "getrawtransaction",
        "getgovproposal",
        "listgovproposals",
        "listgovproposalvotes",
        "vmmap",
        "gettxout",
    ];

    if !methods.contains(&method) {
        log::debug!("forbidden");
        return Err(Error::Forbidden {
            method: method.to_owned(),
        });
    }

    Ok(())
}

#[ocean_endpoint]
async fn rpc(
    Extension(ctx): Extension<Arc<AppContext>>,
    Json(body): Json<RpcDto>,
) -> Result<ApiRpcResponse<serde_json::Value>> {
    method_whitelist(&body.method)?;

    let res: serde_json::Value = ctx.client.call(&body.method, &body.params).await?;

    Ok(ApiRpcResponse::new(res))
}

pub fn router(ctx: Arc<AppContext>) -> Router {
    Router::new().route("/", post(rpc)).layer(Extension(ctx))
}
