# Hard Forks

This document lists all the hard forks in DeFiChain history and their changes.

## Ang Mo Kio (v1.2.0)

_Block activated: 356500_

### Features

- Support for DeFi Standard Token (DST), DeFi Custom Token (DCT) and DeFi Asset Token (DAT)
- The ability to create, mint and distribute tokens has opened to all users.
- Bitcoin anchor rewards in block reward
- Reserve 25% of block reward for incentive funding

## Bayfront (v1.3.0)

_Block activated: 405,000_

### Features

- DeX and liquidity pools
    - Ability to swap between tokens
    - Earn returns by providing liquidity through trading fees and liquidity incentives

## Bayfront Gardens (v1.3.8)

_Block activated: 488,300_

- Fixed issue with rewards distribution where pool shares of less than 0.01% weren't receiving rewards
- Improved poolswap performance
- Added new RPC calls to improve wallet usability

## Clarke Quay (v1.4.0)

_Block activated: 595,738_

### Features

- Add custom rewards to pool pairs ([dfip#3](https://github.com/DeFiCh/dfips/issues/5))
- Fix auto selecting balances
- Do not stake while downloading blocks

## Dakota (v1.5.0)

_Block activated: 678,000_

### Features

- Reduce masternode collateral requirement from 100,000 DFI to 20,000 DFI
- Full refactor of the anchoring system, for more stability and robustness
- Fix swap max price calculation. Fixes an issue where slippage protection was not activated correctly during times where the pool liquidity was low
- Fix an issue where certain transactions were occasionally stalling the blockchain
- Increase performance while staking

## Dakota Crescent (v1.6.0)

_Block activated: 733,000_

### Features

- Consensus efficiency improvements

## Eunos (v1.7.0)

_Block activated: 894,000_

### Features

- Interchain Exchange
- Update block reward scheme ([dfip#8](https://github.com/DeFiCh/dfips/issues/18))
    - Introduce incentives for atomic swap, futures and options
    - Reduce block emmisions by 1.658% every 32,690 blocks
- Burn foundation funds ([dfip#7](https://github.com/DeFiCh/dfips/issues/17))
- Price oracles
- Track amount of funds burnt
- Performance improvements in calculating block reward
- Increase difficulty adjustment period from 10 blocks to 1008 blocks
- Introduce 1008 block delay to masternode registration, 2016 block delay to resignation

## Eunos Paya (v1.8.0)

_Block activated: 1,072,000_

### Features

- Introduce 5 and 10 year lock for masternodes for increased staking power
- `listaccounthistory` shows pool reward types as block reward or swap fees (commission)
- Rename `getmintinginfo` to `getmininginfo`
- Add pagination to `listoracles`, `listlatestrawprices` and `listprices` RPCs
- Clear anchor and SPV dasebase when SPV database version changes
- Add support for subnode staking to make locked masternode rewards consistent
    - Normal masternodes mines using 2 subnodes.
    - 5-year lockup masternode mines using 3 subnodes.
    - 10-year lockup masternode mines using 4 subnodes.

## Fort Canning (v2.0.0)

_Block activated: 1,367,000_

### Features

- Decentralised loans
    - Collateral vaults
    - Taking loans and repayments
    - Liquidations and Auctions
- Composite swaps, swap across up to 3 pairs
- `spv_refundhtlcall` RPC gets all HTLC contracts stored in wallet and creates refunds transactions for all that have expired
- Anchoring cost has now been reduced to BTC dust (minimum possible)
- Transaction fees which were being burnt after Eunos upgrade will now be included in block rewards again

## Fort Canning Road (v2.7.0)

_Block activated: 1,786,000_

### Features

- Futures contracts
- Allow loans in a vault to be payed back with any other token through swaps to DFI
- Allow DeX fees to be applied per pool and per token (affects all pools token is in)

## Fort Canning Crunch (v2.8.0)

_Block activated: 1,936,000_

### Features

- Support for token splits

## Fort Canning Spring (v2.9.0)

_Block activated: 2,033,000_

### Features

- DFI-DUSD future swaps
- Convert liquidation penalties to DUSD before burning
- Add DUSD swap fee
- Add `-dexstats` flag
- Add token split information to account history.

## Fort Canning Great World (v2.10.0)

_Block activated: 2,212,000_

### Features

- Negative interest rates

## Fort Canning Epilogue (v2.11.0)

_Block activated: 2,257,500_

### Features

- Update vault collateral requirement rules
- Allow payback of DUSD with vault's collateral
- Allow higher collateral factor

## Grand Central (v3.0.1)

_Block activated: 2,479,000_

### Features

- On-chain governance
- Token consortium framework
- Support for masternode parameter updates (owner, operator, reward addresses)
- Pool commission and reward fixes

## Grand Central Epilogue (v3.2.0)

_Block activated: 2,574,000_

### Features

- Mint tokens to any address

## Metachain (v4.0.0)

_Block activated: 3,462,000_

### Features

- EVM support
- DUSD looped vaults