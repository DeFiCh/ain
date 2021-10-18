# test-loan-cli
DeFichain CLI helper to test loan and vault features.

## Build

`cargo build --release --target-dir <dir>`

Get usage with ./\<dir>/release/test-loan-cli --help


## Requirements

This CLI assumes that you are running defid and set password and user in defi.conf

```
cat defi.conf
rpcpassword=test
rpcuser=test
```

Some commands require foundation auth :
- createauction
- createloanscheme
- createloantoken

## Example

```
test-loan-cli createloanscheme -n 10 # Create 10 different loan scheme

test-loan-cli createloantoken --all # Create 14 loan token taken from NASDAQ 100

test-loan-cli createvault # Create a vault for each loan scheme

test-loan-cli createauction --token TSLA # Create and liquidate a vault against the loan token passed as argument.
```
