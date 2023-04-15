impl serde :: Serialize for crate :: codegen :: types :: EthChainIdResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . id . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthCallResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . data . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthSignResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . signature . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthGetBalanceResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . balance . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthSendTransactionResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . hash . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthCoinBaseResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . address . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthMiningResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . is_mining . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthHashRateResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . hash_rate . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthGasPriceResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . gas_price . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthBlockNumberResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . block_number . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthGetTransactionCountResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . number_transaction . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthGetBlockTransactionCountByHashResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . number_transaction . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthGetBlockTransactionCountByNumberResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . number_transaction . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthGetUncleCountByBlockHashResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . number_uncles . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthGetUncleCountByBlockNumberResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . number_uncles . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthGetCodeResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . code . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthSignTransactionResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . transaction . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthSendRawTransactionResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . hash . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthEstimateGasResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . gas_used . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthCompileSolidityResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . compiled_code . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthCompileLllResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . compiled_code . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthCompileSerpentResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . compiled_code . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthProtocolVersionResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . protocol_version . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: Web3Sha3Result { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . data . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: NetPeerCountResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . number_peer . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: NetVersionResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . network_version . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: Web3ClientVersionResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . client_version . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthSubmitWorkResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . is_valid . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthSubmitHashrateResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . is_valid . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: EthGetStorageAtResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . value . to_string ()) ; } } impl serde :: Serialize for crate :: codegen :: types :: BlockHashResult { fn serialize < S > (& self , serializer : S) -> Result < S :: Ok , S :: Error > where S : serde :: Serializer , { return serializer . serialize_str (& self . hash . to_string ()) ; } }