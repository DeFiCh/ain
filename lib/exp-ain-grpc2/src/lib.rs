pub fn add(left: usize, right: usize) -> usize {
    left + right
}

pub mod proto {
    // This is the target to be generated.
    include!(concat!(env!("OUT_DIR"), "/eth.rs"));
}

#[cfg(test)]
mod tests {

    use super::*;

    #[test]
    fn it_works() {
        let result = add(2, 2);
        assert_eq!(result, 4);

        let x = proto::EthCallResult::default();
        println!("{:?}", x);
    }
}
