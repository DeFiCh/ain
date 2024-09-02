use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
    Json,
};
use serde::Serialize;
use serde_json::json;
use snafu::{Location, Snafu};

// #[macro_export]
// macro_rules! over_underflow_msg {
//     ($op:expr, $k1:expr, $v1:expr, $k2:expr, $v2:expr) => {
//         format!("Try to {} {}: {:?} with {}: {:?}", $op, $k1, $v1, $k2, $v2)
//     }
// }

#[derive(Debug)]
pub enum IndexAction {
    Index,
    Invalidate,
}

impl std::fmt::Display for IndexAction {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        match self {
            IndexAction::Index => write!(f, "index"),
            IndexAction::Invalidate => write!(f, "invalidate"),
        }
    }
}

#[derive(Snafu, Debug)]
pub enum NotFoundKind {
    Auction,
    Proposal,
    Masternode,
    Scheme,
    Oracle,
    Token { id: String },
    PoolPair,
    RawTx,
}

#[derive(Debug, Snafu)]
#[snafu(visibility(pub))]
pub enum Error {
    BincodeError {
        #[snafu(source)]
        error: bincode::Error,
        #[snafu(implicit)]
        location: Location,
    },
    BitcoinAddressError {
        #[snafu(source)]
        error: bitcoin::address::Error,
        #[snafu(implicit)]
        location: Location,
    },
    BitcoinAddressParseError {
        #[snafu(source)]
        error: bitcoin::address::error::ParseError,
        #[snafu(implicit)]
        location: Location,
    },
    BitcoinConsensusEncodeError {
        #[snafu(source)]
        error: bitcoin::consensus::encode::Error,
        #[snafu(implicit)]
        location: Location,
    },
    BitcoinHexToArrayError {
        #[snafu(source)]
        error: bitcoin::hex::HexToArrayError,
        #[snafu(implicit)]
        location: Location,
    },
    DecimalError {
        #[snafu(source)]
        error: rust_decimal::Error,
        #[snafu(implicit)]
        location: Location,
    },
    // #[snafu(context(false))]
    // #[snafu(transparent)]
    DBError {
        #[snafu(source)]
        error: ain_db::DBError,
        #[snafu(implicit)]
        location: Location,
    },
    FromHexError {
        #[snafu(source)]
        error: hex::FromHexError,
        #[snafu(implicit)]
        location: Location,
    },
    IOError {
        #[snafu(source)]
        error: std::io::Error,
        #[snafu(implicit)]
        location: Location,
    },
    JsonrpseeError {
        #[snafu(source)]
        error: jsonrpsee::core::Error,
        #[snafu(implicit)]
        location: Location,
    },
    ParseIntError {
        #[snafu(source)]
        error: std::num::ParseIntError,
        #[snafu(implicit)]
        location: Location,
    },
    ParseFloatError {
        #[snafu(source)]
        error: std::num::ParseFloatError,
        #[snafu(implicit)]
        location: Location,
    },
    RpcError {
        #[snafu(source)]
        error: defichain_rpc::Error,
        #[snafu(implicit)]
        location: Location,
    },
    SerdeJsonError {
        #[snafu(source)]
        error: serde_json::Error,
        #[snafu(implicit)]
        location: Location,
    },
    TryFromIntError {
        #[snafu(source)]
        error: std::num::TryFromIntError,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(display("{} not found", kind))]
    NotFound {
        kind: NotFoundKind,
    },
    // #[error(
    //     "attempting to sync: {0:?} but type: {1:?} with id: {2:?} cannot be found in the index"
    // )]
    // NotFoundIndex(IndexAction, String, String),
    NotFoundIndex,
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
    #[snafu(display("Invalid token currency format: {item}"))]
    InvalidTokenCurrency {
        item: String,
    },
    #[snafu(display("Invalid fixed interval price format: {item}"))]
    InvalidFixedIntervalPrice {
        item: String,
    },
    #[snafu(display("Invalid amount format: {item}"))]
    InvalidAmount {
        item: String,
    },
    #[snafu(display("Invalid pool pair symbol format: {item}"))]
    InvalidPoolPairSymbol {
        item: String,
    },
    #[snafu(display("To primitive error: {msg}"))]
    ToPrimitiveError {
        msg: String,
        #[snafu(implicit)]
        location: Location,
    },
    #[snafu(display("error message: {}", msg))]
    Other {
        msg: String,
    },
}

impl From<bincode::Error> for Error {
    fn from(error: bincode::Error) -> Self {
        Self::BincodeError {
            error,
            location: snafu::location!(),
        }
    }
}

impl From<bitcoin::address::Error> for Error {
    fn from(error: bitcoin::address::Error) -> Self {
        Self::BitcoinAddressError {
            error,
            location: snafu::location!(),
        }
    }
}

impl From<bitcoin::address::error::ParseError> for Error {
    fn from(error: bitcoin::address::error::ParseError) -> Self {
        Self::BitcoinAddressParseError {
            error,
            location: snafu::location!(),
        }
    }
}

impl From<bitcoin::consensus::encode::Error> for Error {
    fn from(error: bitcoin::consensus::encode::Error) -> Self {
        Self::BitcoinConsensusEncodeError {
            error,
            location: snafu::location!(),
        }
    }
}

impl From<bitcoin::hex::HexToArrayError> for Error {
    fn from(error: bitcoin::hex::HexToArrayError) -> Self {
        Self::BitcoinHexToArrayError {
            error,
            location: snafu::location!(),
        }
    }
}

impl From<rust_decimal::Error> for Error {
    fn from(error: rust_decimal::Error) -> Self {
        Self::DecimalError {
            error,
            location: snafu::location!(),
        }
    }
}

impl From<ain_db::DBError> for Error {
    fn from(error: ain_db::DBError) -> Self {
        Self::DBError {
            error,
            location: snafu::location!(),
        }
    }
}

impl From<hex::FromHexError> for Error {
    fn from(error: hex::FromHexError) -> Self {
        Self::FromHexError {
            error,
            location: snafu::location!(),
        }
    }
}

impl From<std::io::Error> for Error {
    fn from(error: std::io::Error) -> Self {
        Self::IOError {
            error,
            location: snafu::location!(),
        }
    }
}

impl From<jsonrpsee::core::Error> for Error {
    fn from(error: jsonrpsee::core::Error) -> Self {
        Self::JsonrpseeError {
            error,
            location: snafu::location!(),
        }
    }
}

impl From<std::num::ParseIntError> for Error {
    fn from(error: std::num::ParseIntError) -> Self {
        Self::ParseIntError {
            error,
            location: snafu::location!(),
        }
    }
}

impl From<std::num::ParseFloatError> for Error {
    fn from(error: std::num::ParseFloatError) -> Self {
        Self::ParseFloatError {
            error,
            location: snafu::location!(),
        }
    }
}

impl From<defichain_rpc::Error> for Error {
    fn from(error: defichain_rpc::Error) -> Self {
        Self::RpcError {
            error,
            location: snafu::location!(),
        }
    }
}

impl From<serde_json::Error> for Error {
    fn from(error: serde_json::Error) -> Self {
        Self::SerdeJsonError {
            error,
            location: snafu::location!(),
        }
    }
}

impl From<std::num::TryFromIntError> for Error {
    fn from(error: std::num::TryFromIntError) -> Self {
        Self::TryFromIntError {
            error,
            location: snafu::location!(),
        }
    }
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
    pub fn into_code_and_message(self) -> (StatusCode, String) {
        let (code, reason) = match &self {
            Error::NotFound { kind: _ } => (StatusCode::NOT_FOUND, format!("{self}")),
            Error::BadRequest { msg } => (StatusCode::BAD_REQUEST, msg.clone()),
            _ => (StatusCode::INTERNAL_SERVER_ERROR, self.to_string()),
        };
        (code, reason)
    }
}
