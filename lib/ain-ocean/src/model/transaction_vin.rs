#[derive(Debug, Default)]
pub struct TransactionVin {
    pub id: String,
    pub txid: String,
    pub coinbase: String,
    pub vout: TransactionVinVout,
    pub script: TransactionVinScript,
    pub tx_in_witness: Vec<String>,
    pub sequence: String,
}

#[derive(Debug, Default)]
pub struct TransactionVinVout {
    pub id: String,
    pub txid: String,
    pub n: i32,
    pub value: String,
    pub token_id: i32,
    pub script: TransactionVinVoutScript,
}

#[derive(Debug, Default)]
pub struct TransactionVinScript {
    pub hex: String,
}

#[derive(Debug, Default)]
pub struct TransactionVinVoutScript {
    pub hex: String,
}
