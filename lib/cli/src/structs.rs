use ethereum::AccessListItem;
use primitive_types::H160;
use primitive_types::U256;
use structopt::StructOpt;

#[derive(Debug)]
pub struct HexData(Vec<u8>);

pub type AccessList = Vec<AccessListItem>;

#[derive(Debug, StructOpt)]
pub struct CallRequest {
    /// The sender of the transaction (optional).
    #[structopt(long, parse(try_from_str = parse_h160))]
    pub from: Option<H160>,

    /// The recipient of the transaction (optional).
    #[structopt(long, parse(try_from_str = parse_h160))]
    pub to: Option<H160>,

    /// The gas price for the transaction (optional).
    #[structopt(long, parse(try_from_str = parse_u256))]
    pub gas_price: Option<U256>,

    /// The maximum base fee per gas the caller is willing to pay (optional, used for EIP-1559 transactions).
    #[structopt(long = "max-fee-per-gas", parse(try_from_str = parse_u256))]
    pub max_fee_per_gas: Option<U256>,

    /// The priority fee per gas the caller is paying to the block author (optional, used for EIP-1559 transactions).
    #[structopt(long = "max-priority-fee-per-gas", parse(try_from_str = parse_u256))]
    pub max_priority_fee_per_gas: Option<U256>,

    /// The amount of gas provided for the transaction (optional).
    #[structopt(long, parse(try_from_str = parse_u256))]
    pub gas: Option<U256>,

    /// The amount of DFI (in Wei) sent with the transaction (optional).
    #[structopt(long, parse(try_from_str = parse_u256))]
    pub value: Option<U256>,

    /// The input data for the transaction (optional).
    #[structopt(long, parse(try_from_str = parse_hex_data))]
    pub data: Option<HexData>,

    /// The nonce value for the transaction (optional).
    #[structopt(long, parse(try_from_str = parse_u256))]
    pub nonce: Option<U256>,

    /// The access list for the transaction (optional, used for EIP-2930 and EIP-1559 transactions).
    #[structopt(long, parse(try_from_str = parse_access_list))]
    pub access_list: Option<AccessList>,

    /// The EIP-2718 transaction type (optional, used for typed transactions).
    #[structopt(long, parse(try_from_str = parse_u256))]
    pub transaction_type: Option<U256>,
}

fn parse_h160(s: &str) -> Result<H160, String> {
    s.parse::<H160>()
        .map_err(|e| format!("Failed to parse H160: {e}"))
}

fn parse_u256(s: &str) -> Result<U256, String> {
    s.parse::<U256>()
        .map_err(|e| format!("Failed to parse U256: {e}"))
}

fn parse_hex_data(s: &str) -> Result<HexData, String> {
    if let Some(stripped) = s.strip_prefix("0x") {
        let hex = hex::decode(stripped).map_err(|e| format!("Failed to parse hex data: {e}"))?;
        Ok(HexData(hex))
    } else {
        Err("Data must start with 0x".to_string())
    }
}

fn parse_access_list(s: &str) -> Result<Vec<AccessListItem>, String> {
    serde_json::from_str(s).map_err(|e| format!("Failed to parse access list: {e}"))
}

impl From<CallRequest> for ain_grpc::call_request::CallRequest {
    fn from(val: CallRequest) -> Self {
        ain_grpc::call_request::CallRequest {
            from: val.from,
            to: val.to,
            gas_price: val.gas_price,
            max_fee_per_gas: val.max_fee_per_gas,
            max_priority_fee_per_gas: val.max_priority_fee_per_gas,
            gas: val.gas,
            value: val.value,
            data: val.data.map(|hex| hex.0),
            nonce: val.nonce,
            access_list: val.access_list,
            transaction_type: val.transaction_type,
        }
    }
}
