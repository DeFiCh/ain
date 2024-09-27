use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
    Json,
};
use serde::Serialize;
use serde_json::json;
use snafu::{Location, Snafu};

#[derive(Debug)]
pub enum IndexAction {
    Index,
    Invalidate,
}

impl std::fmt::Display for IndexAction {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            Self::Index => write!(f, "index"),
            Self::Invalidate => write!(f, "invalidate"),
        }
    }
}

#[derive(Snafu, Debug)]
pub enum NotFoundKind {
    #[snafu(display("auction"))]
    Auction,
    #[snafu(display("collateral token"))]
    CollateralToken,
    #[snafu(display("loan token"))]
    LoanToken,
    #[snafu(display("masternode"))]
    Masternode,
    #[snafu(display("oracle"))]
    Oracle,
    #[snafu(display("poolpair"))]
    PoolPair,
    #[snafu(display("proposal"))]
    Proposal,
    #[snafu(display("rawtx"))]
    RawTx,
    #[snafu(display("scheme"))]
    Scheme,
    #[snafu(display("token {}", id))]
    Token { id: String },
    #[snafu(display("vault"))]
    Vault,
}

#[derive(Debug, Snafu)]
#[snafu(visibility(pub))]
pub enum Error {
    #[snafu(context(false))] // allows using the ? operator directly on the underlying error
    BincodeError {
        #[snafu(source)]
        error: bincode::Error,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(context(false))]
    BitcoinAddressError {
        #[snafu(source)]
        error: bitcoin::address::Error,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(context(false))]
    #[snafu(display("InvalidDefiAddress"))]
    BitcoinAddressParseError {
        #[snafu(source)]
        error: bitcoin::address::error::ParseError,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(context(false))]
    BitcoinConsensusEncodeError {
        #[snafu(source)]
        error: bitcoin::consensus::encode::Error,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(context(false))]
    BitcoinHexToArrayError {
        #[snafu(source)]
        error: bitcoin::hex::HexToArrayError,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(context(false))]
    DecimalError {
        #[snafu(source)]
        error: rust_decimal::Error,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(context(false))]
    DBError {
        #[snafu(source)]
        error: ain_db::DBError,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(context(false))]
    FromHexError {
        #[snafu(source)]
        error: hex::FromHexError,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(context(false))]
    IOError {
        #[snafu(source)]
        error: std::io::Error,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(context(false))]
    JsonrpseeError {
        #[snafu(source)]
        error: jsonrpsee::core::Error,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(context(false))]
    ParseIntError {
        #[snafu(source)]
        error: std::num::ParseIntError,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(context(false))]
    ParseFloatError {
        #[snafu(source)]
        error: std::num::ParseFloatError,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(context(false))]
    RpcError {
        #[snafu(source)]
        error: defichain_rpc::Error,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(context(false))]
    SerdeJsonError {
        #[snafu(source)]
        error: serde_json::Error,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(context(false))]
    TryFromIntError {
        #[snafu(source)]
        error: std::num::TryFromIntError,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(display("Unable to find {}", kind))]
    NotFound {
        kind: NotFoundKind,
    },
    NotFoundMessage {
        msg: String,
    },
    #[snafu(display(
        "attempting to sync: {:?} but type: {} with id: {} cannot be found in the index",
        action,
        r#type,
        id
    ))]
    NotFoundIndex {
        action: IndexAction,
        r#type: String,
        id: String,
    },
    DecimalConversionError,
    #[snafu(display("Arithmetic overflow"))]
    ArithmeticOverflow {
        // msg: String, // TODO(canonbrother): less complicated atm
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(display("Arithmetic underflow"))]
    ArithmeticUnderflow {
        // msg: String, // TODO(canonbrother): less complicated atm
        #[snafu(implicit)]
        location: Location,
    },
    SecondaryIndex,
    BadRequest {
        msg: String,
    },
    #[snafu(display("Invalid token currency format: {}", item))]
    InvalidTokenCurrency {
        item: String,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(display("Invalid fixed interval price format: {}", item))]
    InvalidFixedIntervalPrice {
        item: String,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(display("Invalid amount format: {}", item))]
    InvalidAmount {
        item: String,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(display("Invalid pool pair symbol format: {}", item))]
    InvalidPoolPairSymbol {
        item: String,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(display("To primitive error: {}", msg))]
    ToPrimitiveError {
        msg: String,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(display("{}", msg))]
    Other {
        msg: String,
    },
}

impl From<Box<dyn std::error::Error>> for Error {
    fn from(err: Box<dyn std::error::Error>) -> Self {
        Self::Other {
            msg: err.to_string(),
        }
    }
}

impl From<&str> for Error {
    fn from(s: &str) -> Self {
        Self::Other { msg: s.to_string() }
    }
}

#[derive(Serialize)]
pub enum ErrorKind {
    NotFound,
    BadRequest,
    Unknown,
}

#[derive(Serialize)]
struct ApiErrorData {
    code: u16,
    r#type: ErrorKind,
    at: u128,
    message: String,
    url: String,
}
#[derive(Serialize)]
pub struct ApiError {
    error: ApiErrorData,
    #[serde(skip)]
    status: StatusCode,
}

impl ApiError {
    #[must_use]
    pub fn new(status: StatusCode, message: String, url: String) -> Self {
        let current_time = std::time::SystemTime::now();
        let at = current_time
            .duration_since(std::time::UNIX_EPOCH)
            .expect("Time went backwards")
            .as_millis();

        let r#type = match status {
            StatusCode::NOT_FOUND => ErrorKind::NotFound,
            StatusCode::BAD_REQUEST => ErrorKind::BadRequest,
            _ => ErrorKind::Unknown,
        };

        Self {
            error: ApiErrorData {
                r#type,
                code: status.as_u16(),
                message,
                url,
                at,
            },
            status,
        }
    }
}

impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        let status = self.status;
        let body = Json(json!({
            "error": self.error
        }));
        (status, body).into_response()
    }
}

impl Error {
    #[must_use]
    pub fn into_code_and_message(self) -> (StatusCode, String) {
        let (code, reason) = match &self {
            Self::NotFound { kind: _ } => (StatusCode::NOT_FOUND, format!("{self}")),
            Self::NotFoundMessage { msg } => (StatusCode::NOT_FOUND, msg.clone()),
            Self::BadRequest { msg } => (StatusCode::BAD_REQUEST, msg.clone()),
            Self::Other { msg } => (StatusCode::INTERNAL_SERVER_ERROR, msg.clone()),
            _ => (StatusCode::INTERNAL_SERVER_ERROR, self.to_string()),
        };
        (code, reason)
    }
}
