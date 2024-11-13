use ain_macros::ConsensusEncoding;
use bitcoin::io;
use bitcoin::{consensus::Decodable, ScriptBuf};
use std::io::BufRead;

#[derive(Debug)]
pub struct RawDbEntry {
    pub prefix: u8,
    pub key: Vec<u8>,
    pub value: Vec<u8>,
}

impl RawDbEntry {
    pub fn parse(input: &str) -> Result<Self, Box<dyn std::error::Error>> {
        let parts: Vec<&str> = input.split_whitespace().collect();
        if parts.len() != 3 {
            return Err("Invalid input format: expected 3 space-separated parts".into());
        }

        let prefix = u8::from_str_radix(parts[0], 16)?;

        let key = hex::decode(parts[1])?;
        let value = hex::decode(parts[2])?;

        Ok(RawDbEntry { prefix, key, value })
    }
}

#[derive(ConsensusEncoding, Debug, Clone, PartialEq, Eq)]
pub struct BalanceKey {
    pub owner: ScriptBuf,
    pub token_id: u32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PrefixedData<K, V> {
    pub key: K,
    pub value: V,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Prefix {
    ByBalance(PrefixedData<BalanceKey, i64>),
    ByHeight(PrefixedData<ScriptBuf, u32>),
    VMDomainTxEdge(PrefixedData<(u8, String), String>),
    VMDomainBlockEdge(PrefixedData<(u8, String), String>),
}

impl TryFrom<RawDbEntry> for Prefix {
    type Error = bitcoin::consensus::encode::Error;

    fn try_from(value: RawDbEntry) -> Result<Self, Self::Error> {
        match value.prefix {
            b'a' => Ok(Prefix::ByBalance(PrefixedData::try_from(value)?)),
            b'b' => Ok(Prefix::ByHeight(PrefixedData::try_from(value)?)),
            b'e' => Ok(Prefix::VMDomainTxEdge(PrefixedData::try_from(value)?)),
            b'N' => Ok(Prefix::VMDomainBlockEdge(PrefixedData::try_from(value)?)),
            _ => Err(bitcoin::consensus::encode::Error::ParseFailed(
                "Unknown prefix",
            )),
        }
    }
}

impl<K, V> TryFrom<RawDbEntry> for PrefixedData<K, V>
where
    K: Decodable,
    V: Decodable,
{
    type Error = bitcoin::consensus::encode::Error;

    fn try_from(value: RawDbEntry) -> Result<Self, Self::Error> {
        let mut key_slice = value.key.as_slice();
        let mut value_slice = value.value.as_slice();

        let key = K::consensus_decode(&mut key_slice)?;
        let value = V::consensus_decode(&mut value_slice)?;
        Ok(Self { key, value })
    }
}

fn process_line(line: &str) -> Result<(), Box<dyn std::error::Error>> {
    let raw_entry = RawDbEntry::parse(line)?;

    match Prefix::try_from(raw_entry) {
        Ok(entry) => println!("{entry:?}"),
        Err(_) => {}
    }

    Ok(())
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let stdin = std::io::stdin();
    let reader = stdin.lock();

    for line in reader.lines() {
        let line = line?;
        if !line.trim().is_empty() {
            if let Err(e) = process_line(&line) {
                eprintln!("Error processing line: {}", e);
            }
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse() {
        let input = "65 014031386566363362646661313837366264383735333933636363626133306430356465356238306261643161646536616131383931376261313939343532353163 4063343432376563623137373563333038346637313439383235313965616161323665636536643561653462303534626239393561326434666130346330353065";

        let raw_entry = RawDbEntry::parse(input).unwrap();
        println!("raw_entry : {:?}", raw_entry);
        let entry = Prefix::try_from(raw_entry).unwrap();

        println!("entry : {:?}", entry);
    }
}
