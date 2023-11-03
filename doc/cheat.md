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

