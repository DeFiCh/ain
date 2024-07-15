use sha2::{Digest, Sha256};

pub fn as_sha256(bytes: Vec<u8>) -> String {
    let mut hasher = Sha256::new();
    hasher.update(bytes);
    format!("{:x}", hasher.finalize())
}
