# Setup nodes

### Prerequisites
- 8GB memory
- 50GB storage
- Active internet connection

### Setup
- Download the [latest release](https://github.com/deFiCh/ain/releases/latest), or [compile](./cheat.md#build) the project to obtain the `defid` binary.
- Configure the node by creating a config file. Important configuration parameters available can be found [here](#configuration).
    ```bash
    touch ~/.defi/defi.conf
    ```
- Run the node.
    ```bash
    defid
    ```

### Configuration

- `rpcbind` + `ethrpcbind`: changes address to bind JSON-RPC servers to. Set this to `0.0.0.0/0` to make the server publicly accessibly.
- `rpcallowip`: whitelist for IP addresses allowed to use the JSON-RPC server. Set this to `0.0.0.0/0` to open server to the public.
- `rpcport` + `ethrpcport` + `grpcport` + `wsport`: change JSON-RPC server ports.
- `rpcuser` + `rpcpassword`: set username/password for JSON-RPC server.
- `txindex`: creates index of every transaction. Allows you to query any transaction without knowing the block hash. Useful for blockchain analysis and explorer nodes.
- `walletfastselect` + `walletcoinopteagerselect`: creates a transaction quicker, but may not use the most optimal UTXO. Useful for masternodes and wallets with a large number of UTXOs.

#### Example Configuration

For exchange nodes,
- do not enable public connections
- use `txindex` to track deposits
- use wallet options to send transactions quicker

```
txindex=1
walletfastselect=1
walletcoinopteagerselect=1
```