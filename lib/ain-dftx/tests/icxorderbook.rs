use ain_macros::test_dftx_serialization;

#[test]
#[test_dftx_serialization]
fn test_icx_create_order() {}

#[test]
#[test_dftx_serialization]
fn test_icx_make_offer() {}

#[test]
#[test_dftx_serialization]
fn test_icx_submit_dfc_htlc() {}

#[test]
#[test_dftx_serialization]
fn test_icx_submit_ext_htlc() {}

#[test]
#[test_dftx_serialization]
fn test_icx_claim_dfc_htlc() {}

#[test]
#[test_dftx_serialization]
fn test_icx_close_order() {}

#[test]
#[test_dftx_serialization]
fn test_icx_close_offer() {}
