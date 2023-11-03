# DeFiChain Cheat Sheet

## Get, Build, Compile

### Get

- Curl: `curl -OL https://github.com/DeFiCh/ain/releases/download/v4.0.0/defichain-4.0.0-x86_64-pc-linux-gnu.tar.gz`
- Aria: `aria2c -x16 -s16 https://github.com/DeFiCh/ain/releases/download/v4.0.0/defichain-4.0.0-x86_64-pc-linux-gnu.tar.gz`
- Wget: `wget https://github.com/DeFiCh/ain/releases/download/v4.0.0/defichain-4.0.0-x86_64-pc-linux-gnu.tar.gz`
- 
### Unpack 

- `tar -xvzf defichain-4.0.0.tar.gz`

### Run

- Mainnet: `defid`
- Testnet: `defid -testnet`
- Changi: `defid -changi`
- Regtest: `defid -regtest`

### Interact

- Mainnet: `defi-cli getblockchaininfo`
- Mainnet: `defi-cli -testnet getblockchaininfo`
- Mainnet: `defi-cli -changi getblockchaininfo`
- Mainnet: `defi-cli -regtest getblockchaininfo`

