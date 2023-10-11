use anyhow::{format_err, Error};
use sp_core::U256;

fn fits_word(arr: &[u64; 4]) -> bool {
    for &i in arr.iter().skip(1) {
        if i != 0_u64 {
            return false;
        }
    }

    true
}
pub fn checked_as_u64(num: U256) -> Result<u64, Error> {
    let U256(ref arr) = num;
    if !fits_word(arr) {
        return Err(format_err!("Cannot cast U256 to u64"));
    }

    Ok(num.as_u64())
}

pub fn checked_as_usize(num: U256) -> Result<usize, Error> {
    let U256(ref arr) = num;
    if !fits_word(arr) || arr[0] > usize::MAX as u64 {
        return Err(format_err!("Cannot cast U256 to usize"));
    }

    Ok(num.as_usize())
}

#[cfg(test)]
mod tests {
    use crate::utils::{checked_as_u64, checked_as_usize};
    use sp_core::U256;

    #[test]
    fn test_should_throw_error_u64() {
        let num = U256::MAX;
        let num1 = U256::from(u64::MAX as u128 + 1);

        assert!(checked_as_u64(num).is_err());
        assert!(checked_as_u64(num1).is_err());
    }

    #[test]
    fn test_should_throw_error_usize() {
        let num = U256::MAX;
        let num1 = U256::from(usize::MAX as u128 + 1);

        assert!(checked_as_usize(num).is_err());
        assert!(checked_as_u64(num1).is_err());
    }

    #[test]
    fn test_u64() {
        let num = U256::one();

        assert_eq!(checked_as_u64(num).unwrap(), 1_u64);
    }

    #[test]
    fn test_usize() {
        let num = U256::one();

        assert_eq!(checked_as_usize(num).unwrap(), 1_usize);
    }
}
