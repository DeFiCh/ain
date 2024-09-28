use sha2::{Digest, Sha256};

#[must_use]
pub fn as_sha256(bytes: &[u8]) -> [u8; 32] {
    let mut hasher = Sha256::new();
    hasher.update(bytes);
    hasher.finalize().into()
}
