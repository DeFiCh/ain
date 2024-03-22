# DeFiChain Cheat Sheet

## Summary

- Quick start
  - [Get](#get)
  - [Run](#run)
  - [Interact](#interact)
- Development 
  - [Build](#build)
  - [Test](#test)
  - [Format](#test)
  - [Lint](#lint)
- Common Usages
  - [Balance / List](#balance--list)
  - [Balance / Transfer / In-domain](#balance--transfer--in-domain)
  - [Balance / Transfer / Cross-domain](#balance--transfer--cross-domain)

## Get

- curl: `curl -OL https://github.com/DeFiCh/ain/releases/download/v4.0.0/defichain-4.0.0-x86_64-pc-linux-gnu.tar.gz`
- aria: `aria2c -x16 -s16 https://github.com/DeFiCh/ain/releases/download/v4.0.0/defichain-4.0.0-x86_64-pc-linux-gnu.tar.gz`
- wget: `wget https://github.com/DeFiCh/ain/releases/download/v4.0.0/defichain-4.0.0-x86_64-pc-linux-gnu.tar.gz`
- docker: `docker run docker.io/defi/defichain` [Runs directly; pass args as needed at the end] 

### Unpack

- `tar -xvzf defichain-4.0.0-x86_64-pc-linux-gnu.tar.gz`

## Run

- mainnet: `defid`
- testnet: `defid -testnet`
- changi: `defid -changi`
- regtest: `defid -regtest`

## Interact

- mainnet: `defi-cli getblockchaininfo`
- testnet: `defi-cli -testnet getblockchaininfo`
- changi: `defi-cli -changi getblockchaininfo`
- regtest: `defi-cli -regtest getblockchaininfo`

## Build

- Dockerized build: `./make.sh docker-release` [No pre-requisites needed other than docker]
- Native build: `./make.sh build`

### Pre-requisites

#### Clean OS / CI / Dedicated containers

- Install pre-req: `./make.sh ci-setup-deps`
- User scope pre-req: `./make.sh ci-setup-user-deps`

#### Native

- OS pre-req: `./make.sh pkg-install-deps`
- OS pre-req, target specific: `pkg-install-deps-<arch>`
- User pre-req: `pkg-user-install-rust`
- User pre-req setup, target specific: `pkg-user-setup-rust`

##### Notes

- `./make.sh` Requirements:
  - Debian 12 / Debian Testing / Ubuntu 20.04, 22.04, 23.10
  - Other: Refer to make.sh file, build manually.
  - For others like WSL, MacOS, works partially once set-up. 

## Test

- All: `./make.sh test`
- C++: `./make.sh test-cpp`
  - Filtered: `./make.sh test-cpp --run_test=<filter>`
  - Eg: `./make.sh test-cpp --run_test=baes58_tests --log_level=all`
- Rust: `./make.sh test-rs`
  - Filtered: `./make.sh test-rs <TODO>`
  - Eg: `./make.sh test-rs <TODO>`
- Python: `./make.sh test-py`
  - Filtered: `./make.sh test-py --filter <filter>`
  - Eg: `./make.sh test-py --filter "feature_evm*"`

## Format

- All: `./make.sh fmt`
  - Lang: `fmt-rs` / `fmt-cpp` / `fmt-py`

## Lint

- All: `./make.sh check`
  - Lang: `check-rs` / `check-cpp` / `check-py`

## Common usages

### Balance / List

#### Mine

- UTXO: `defi-cli getbalance`
- DVM: `defi-cli gettokenbalances {} true true`

#### Any

- UTXO - Known addr: `defi-cli listunspent 1 9999999 '[ "<addr>" ]'`
- UTXO - Known addr: `defi-cli scantxoutset start '[ "addr(<addr>)" ]`
- DVM (all): `defi-cli listaccounts {} false false true`
- DVM (single): `defi-cli getaccount <addr>`

### Balance / Transfer / In-Domain

- UTXO: `defi-cli sendtoaddress <addr> <amount>`
- DVM: `defi-cli sendtokenstoaddress '{}' '{ "<dst-addr>": "<amount>@<token>" }'`

### Balance / Transfer / Cross-Domain

- UTXO-DVM: `defi-cli utxostoaccount '{ "<addr>": "<amount>@<token>" }'`
