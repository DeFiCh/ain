# Metachain JSON-RPC CLI

`metachain-cli` is a command-line interface for interacting with defichain EVM endpoint via JSON-RPC.

## Features

- Supports both mainnet and custom chains (e.g., devnet)

## Usage

To get started with `metachain-cli`, use the following command:

```bash
metachain-cli --help
```

This will display the available options and subcommands for the CLI. To specify a custom chain, use the -c or --chain option followed by the chain name:

```bash
metachain-cli -c devnet <SUBCOMMAND>
```

For detailed information on a specific subcommand, run `metachain-cli <SUBCOMMAND> --help`

## Available subcommands :

| Subcommand                                 | Description                                                               |
|--------------------------------------------|---------------------------------------------------------------------------|
| `accounts`                                 | Returns a list of accounts owned by the client                           |
| `block-number`                             | Returns the number of most recent block                                  |
| `call`                                     | Executes a new message call immediately without creating a transaction on the block chain |
| `chain-id`                                 | Returns the chain ID used by this client                                 |
| `estimate-gas`                             | Generates and returns an estimate of how much gas is necessary to allow the transaction to complete |
| `gas-price`                                | Returns the current price per gas in wei                                 |
| `get-balance`                              | Returns the balance of the account of the given address                  |
| `get-block-by-hash`                        | Returns information about a block by its hash                            |
| `get-block-by-number`                      | Returns information about a block by its block number                    |
| `get-block-transaction-count-by-hash`      | Returns the number of transactions in a block from a block matching the given block hash |
| `get-block-transaction-count-by-number`    | Returns the number of transactions in a block matching the given block number |
| `get-code`                                 | Returns the code at a given address                                      |
| `get-state`                                | Returns an object representing the current state of the Metachain network |
| `get-storage-at`                           | Returns the value from a storage position at a given address             |
| `get-transaction-by-block-hash-and-index`  | Returns information about a transaction by block hash and transaction index position |
| `get-transaction-by-block-number-and-index`| Returns information about a transaction by block number and transaction index position |
| `get-transaction-by-hash`                  | Returns the information about a transaction requested by its transaction hash |
| `get-transaction-count`                    | Returns the number of transactions sent from an address                  |
| `hash-rate`                                | Returns the number of hashes per second the node is mining with          |
| `help`                                     | Prints this message or the help of the given subcommand(s)               |
| `mining`                                   | Returns whether the client is mining or not                              |
| `net-version`                              | Returns the current network version                                      |
| `send-raw-transaction`                     | Sends a signed transaction and returns the transaction hash              |
