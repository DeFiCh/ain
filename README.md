![DeFi Blockchain Logo](doc/img/defichain-logo.svg)

# DeFi Blockchain

[![Build Status](https://travis-ci.com/DeFiCh/ain.svg?branch=master)](https://travis-ci.com/DeFiCh/ain)

https://defichain.io

**NOTE**: _master_ branch is a development branch and is thus _unstable_. Do not run from it. Run or compile from [tagged releases](https://github.com/DeFiCh/ain/releases) instead, unless you know what you are doing.

## What is DeFi Blockchain?

DeFi Blockchain’s primary vision is to enable decentralized finance with Bitcoin-grade security, strength and immutability. It's a blockchain dedicated to fast, intelligent and transparent financial services, accessible by everyone.

For more information:

- Visit the [DeFi Blockchain website](https://defichain.io)
- Read our [white paper](https://defichain.io/white-paper/)

Downloadable binaries are available from the [GitHub Releases](https://github.com/DeFiCh/ain/releases) page. 

### Bitcoin Core

DeFi Blockchain is a fork on [Bitcoin Core](https://github.com/bitcoin/bitcoin) from commit [7d6f63c](https://github.com/bitcoin/bitcoin/commit/7d6f63cc2c2b9c4f07a43619eef0b7314474fffd) – which is slightly after v0.18.1 of Bitcoin Core.

DeFi Blockchain has done significant modifications from Bitcoin Core, for instance:

- Moving from Proof-of-Work to Proof-of-Stake
- Masternode model
- Community fund support
- Bitcoin blockchain block anchoring
- Increased decentralized financial transaction and opcode support, etc.
- Configuration defaults (mainnet ports: `8555/4`, testnet ports: `18555/4`, regnet ports: `19555/4`, etc)

Merges from upstream (Bitcoin Core) will be done selectively if it applies to the improved functionality and security of DeFi Blockchain.

## Quick Start

- [Running a node](./doc/setup-nodes.md)
- [Running a node with docker](./doc/setup-nodes-docker.md)
- [Running a masternode](./doc/setup-masternodes.md)
- [Building from scratch](./doc/build-quick.md)

## License

DeFi Blockchain is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.

## Development Process

The `master` branch is regularly built and tested, but is not guaranteed to be
completely stable. [Tags](https://github.com/DeFiCh/ain/tags) are created
regularly to indicate new official, stable release versions of DeFi Blockchain.

The contribution workflow is described in [CONTRIBUTING.md](CONTRIBUTING.md).

## Questions?

Pull requests are warmly welcomed.

Reach us at [engineering@defichain.io](mailto:engineering@defichain.io) for any questions or collaborations.
