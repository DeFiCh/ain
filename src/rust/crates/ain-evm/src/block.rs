use primitive_types::H256;

// go-ethereum Header:
// TBD -- Fields to support

// Header represents a block header in the Ethereum blockchain.
// type Header struct {
// 	ParentHash  common.Hash    `json:"parentHash"       gencodec:"required"`
// 	UncleHash   common.Hash    `json:"sha3Uncles"       gencodec:"required"`
// 	Coinbase    common.Address `json:"miner"`
// 	Root        common.Hash    `json:"stateRoot"        gencodec:"required"`
// 	TxHash      common.Hash    `json:"transactionsRoot" gencodec:"required"`
// 	ReceiptHash common.Hash    `json:"receiptsRoot"     gencodec:"required"`
// 	Bloom       Bloom          `json:"logsBloom"        gencodec:"required"`
// 	Difficulty  *big.Int       `json:"difficulty"       gencodec:"required"`
// 	Number      *big.Int       `json:"number"           gencodec:"required"`
// 	GasLimit    uint64         `json:"gasLimit"         gencodec:"required"`
// 	GasUsed     uint64         `json:"gasUsed"          gencodec:"required"`
// 	Time        uint64         `json:"timestamp"        gencodec:"required"`
// 	Extra       []byte         `json:"extraData"        gencodec:"required"`
// 	MixDigest   common.Hash    `json:"mixHash"`
// 	Nonce       BlockNonce     `json:"nonce"`

// 	// BaseFee was added by EIP-1559 and is ignored in legacy headers.
// 	BaseFee *big.Int `json:"baseFeePerGas" rlp:"optional"`

// 	// WithdrawalsHash was added by EIP-4895 and is ignored in legacy headers.
// 	WithdrawalsHash *common.Hash `json:"withdrawalsRoot" rlp:"optional"`

// 	/*
// 		TODO (MariusVanDerWijden) Add this field once needed
// 		// Random was added during the merge and contains the BeaconState randomness
// 		Random common.Hash `json:"random" rlp:"optional"`
// 	*/
// }

type MerkleRoot = H256;

#[derive(Default, Clone, Debug, PartialEq, Eq)]
pub struct Header {
    pub hash: H256,
    pub parent_hash: H256,
    pub state_root: MerkleRoot,
    pub nonce: u64,
    pub timestamp: u64,
    pub block_number: u64,
}

impl Header {
    pub fn new() -> Self {
        Header {
            ..Default::default()
        }
    }
}

#[derive(Default, Clone, Debug, PartialEq, Eq)]
pub struct Block {
    pub header: Header,
    pub txs: Vec<H256>,
}

impl Block {
    pub fn new(txs: Vec<H256>) -> Self {
        let header = Header::new();

        Block { header, txs }
    }
}
