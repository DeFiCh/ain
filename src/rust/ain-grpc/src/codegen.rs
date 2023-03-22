use serde::{Deserialize, Serialize};

pub mod types {
    tonic::include_proto!("types");
}

#[allow(clippy::useless_conversion)]
pub mod rpc {
    tonic::include_proto!("rpc");
}

impl Serialize for types::BlockResult {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        if !self.hash.is_empty() {
            return serializer.serialize_str(&self.hash);
        }

        if let Some(ref block) = self.block {
            return block.serialize(serializer);
        }

        serializer.serialize_str("")
    }
}

impl<'de> Deserialize<'de> for types::BlockResult {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        #[derive(Deserialize)]
        #[serde(untagged)]
        #[allow(clippy::large_enum_variant)]
        enum Res {
            Hash(String),
            Block(types::Block),
        }

        match Res::deserialize(deserializer)? {
            Res::Hash(s) => Ok(types::BlockResult {
                hash: s,
                block: None,
            }),
            Res::Block(b) => Ok(types::BlockResult {
                hash: "".into(),
                block: Some(b),
            }),
        }
    }
}

impl Serialize for types::Transaction {
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        if !self.hash.is_empty() {
            return serializer.serialize_str(&self.hash);
        }

        if let Some(ref tx) = self.raw {
            return tx.serialize(serializer);
        }

        serializer.serialize_str("")
    }
}

impl<'de> Deserialize<'de> for types::Transaction {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        #[derive(Deserialize)]
        #[serde(untagged)]
        enum Tx {
            Hash(String),
            Raw(types::RawTransaction),
        }

        match Tx::deserialize(deserializer)? {
            Tx::Hash(s) => Ok(types::Transaction { hash: s, raw: None }),
            Tx::Raw(tx) => Ok(types::Transaction {
                hash: "".into(),
                raw: Some(tx),
            }),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::types::{BlockResult, Transaction};

    #[test]
    fn test_block_result_json() {
        let foo = BlockResult {
            hash: "foobar".into(),
            block: None,
        };
        let res = serde_json::to_value(&foo).unwrap();
        let foo2: BlockResult = serde_json::from_value(res).unwrap();
        assert_eq!(serde_json::to_value(&foo2).unwrap(), "foobar");
    }

    #[test]
    fn test_transaction() {
        let foo = Transaction {
            hash: "booya".into(),
            raw: None,
        };
        let res = serde_json::to_value(&foo).unwrap();
        let foo2: Transaction = serde_json::from_value(res).unwrap();
        assert_eq!(serde_json::to_value(&foo2).unwrap(), "booya");
    }
}
