# DeFiChain Cheat Sheet

## Get, Build, Compile

### Get

- curl: `curl -OL https://github.com/DeFiCh/ain/releases/download/v4.0.0/defichain-4.0.0-x86_64-pc-linux-gnu.tar.gz`
- aria: `aria2c -x16 -s16 https://github.com/DeFiCh/ain/releases/download/v4.0.0/defichain-4.0.0-x86_64-pc-linux-gnu.tar.gz`
- wget: `wget https://github.com/DeFiCh/ain/releases/download/v4.0.0/defichain-4.0.0-x86_64-pc-linux-gnu.tar.gz`
- 
### Unpack 

- `tar -xvzf defichain-4.0.0-x86_64-pc-linux-gnu.tar.gz`

### Run

- mainnet: `defid`
- testnet: `defid -testnet`
- changi: `defid -changi`
- regtest: `defid -regtest`

### Interact

- mainnet: `defi-cli getblockchaininfo`
- testnet: `defi-cli -testnet getblockchaininfo`
- changi: `defi-cli -changi getblockchaininfo`
- regtest: `defi-cli -regtest getblockchaininfo`

### Build

- Dockerized build: `./make.sh docker-release`
- Native build: `./make.sh build`

#### Pre-requisites

##### Clean OS / CI / Dedicated containers

- Install pre-req: `./make.sh ci-setup-deps`
- User scope pre-req: `./make.sh ci-setup-user-deps`

##### Native

- OS pre-req: `./make.sh pkg-install-deps`
- OS pre-req, target specific: `pkg-install-deps-<arch>`
- User pre-req: `pkg-user-install-rust`
- User pre-req setup, target specific: `pkg-user-setup-rust`

#### Notes

- `./make.sh` supported OS:
  - Debian 12
  - Debian Testing
  - Ubuntu 20.04
  - Ubuntu 22.04
  - Ubuntu 23.10
  - Other hosts OS / targets: Refer to make.sh for process and build manually.

### Test

- All: `./make.sh test`
- C++: `./make.sh test-cpp`
  - Filtered: `./make.sh test-cpp --run_test=<filter>`
  - Filtered: `./make.sh test-cpp --run_test=baes58_tests --log_level=all`
- Rust: `./make.sh test-rs`
  - Filtered: `./make.sh test-rs <TODO>`
  - Filtered: `./make.sh test-rs <TODO>`
- Python: `./make.sh test-py`
  - Filtered: `./make.sh test-py --filter <filter>`
  - Filtered: `./make.sh test-py --filter "feature_evm*"`

### Development

- Check pre-push: `./make.sh check`
- Format all code: `./make.sh fmt`
  - Lang: `fmt-rs/fmt-cpp/fmt-py`
