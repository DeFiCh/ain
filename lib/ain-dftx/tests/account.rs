use ain_macros::test_dftx_serialization;

#[test]
#[test_dftx_serialization]
fn test_utxos_to_account() {}

#[test]
#[test_dftx_serialization]
fn test_account_to_utxos() {}

#[test]
#[test_dftx_serialization]
fn test_account_to_account() {}

#[test]
#[test_dftx_serialization]
fn test_transfer_domain() {}
