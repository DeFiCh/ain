#!/usr/bin/env bash

# setup_var
ownerauthaddr="mwsZw8nF7pKxWH8eoKL9tPxTpaFkz7QeLU"
ownerauthpriv="cRiRQ9cHmy5evDqNDdEV8f6zfbK6epi9Fpz4CRZsmLEmkwy54dWz"
operatorauthaddr="mswsMVsyGMj1FzDMbbxw2QW3KvQAv2FKiy"
operatorauthpriv="cPGEaz8AGiM71NGMRybbCqFNRcuUhg3uGvyY4TFE1BZC26EW2PkC"
alice="0x9b8a4af42140d8a4c153a822f02571a1dd037e89"
bob="0x6c34cbb9219d8caa428835d2073e8ec88ba0a110"
privkey_alice="af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23"
privkey_bob="17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35"
contract_counter="0x60c0604052600760808190526621b7bab73a32b960c91b60a090815261002891600091906100ab565b50600060025534801561003a57600080fd5b50600180546001600160a01b031916331790556040517ff15da729ec5b36e9bda8b3f71979cdac5d0f3169f8590778ac0cd82cc5cc1d4a9061009e906020808252600e908201526d2432b63637961021b7bab73a32b960911b604082015260600190565b60405180910390a161017f565b8280546100b790610144565b90600052602060002090601f0160209004810192826100d9576000855561011f565b82601f106100f257805160ff191683800117855561011f565b8280016001018555821561011f579182015b8281111561011f578251825591602001919060010190610104565b5061012b92915061012f565b5090565b5b8082111561012b5760008155600101610130565b60028104600182168061015857607f821691505b6020821081141561017957634e487b7160e01b600052602260045260246000fd5b50919050565b6103e68061018e6000396000f3fe608060405234801561001057600080fd5b506004361061009e5760003560e01c80638da5cb5b116100665780638da5cb5b146100f4578063a87d942c1461011f578063c8a4ac9c14610127578063d14e62b81461013a578063ee82ac5e1461014d5761009e565b806306fdde03146100a3578063119fbbd4146100c15780631a93d1c3146100cb578063672d5d3b146100db5780638361ff9c146100e1575b600080fd5b6100ab61015f565b6040516100b891906102d5565b60405180910390f35b6100c96101ed565b005b455b6040519081526020016100b8565b436100cd565b6100cd6100ef36600461029c565b610207565b600154610107906001600160a01b031681565b6040516001600160a01b0390911681526020016100b8565b6002546100cd565b6100cd6101353660046102b4565b61026d565b6100c961014836600461029c565b610280565b6100cd61015b36600461029c565b4090565b6000805461016c9061035f565b80601f01602080910402602001604051908101604052809291908181526020018280546101989061035f565b80156101e55780601f106101ba576101008083540402835291602001916101e5565b820191906000526020600020905b8154815290600101906020018083116101c857829003601f168201915b505050505081565b6001600260008282546102009190610328565b9091555050565b6000600a8211156102695760405162461bcd60e51b815260206004820152602260248201527f56616c7565206d757374206e6f742062652067726561746572207468616e2031604482015261181760f11b606482015260840160405180910390fd5b5090565b60006102798284610340565b9392505050565b6001546001600160a01b0316331461029757600080fd5b600255565b6000602082840312156102ad578081fd5b5035919050565b600080604083850312156102c6578081fd5b50508035926020909101359150565b6000602080835283518082850152825b81811015610301578581018301518582016040015282016102e5565b818111156103125783604083870101525b50601f01601f1916929092016040019392505050565b6000821982111561033b5761033b61039a565b500190565b600081600019048311821515161561035a5761035a61039a565b500290565b60028104600182168061037357607f821691505b6020821081141561039457634e487b7160e01b600052602260045260246000fd5b50919050565b634e487b7160e01b600052601160045260246000fdfea2646970667358221220698216ef3f0a0eede3cc4f13017b1d1699d56b3aa4aa8491a3f47fc2d37ac22164736f6c63430008020033"
contract_countercaller="0x608060405234801561001057600080fd5b5060405161025c38038061025c83398101604081905261002f91610054565b600080546001600160a01b0319166001600160a01b0392909216919091179055610082565b600060208284031215610065578081fd5b81516001600160a01b038116811461007b578182fd5b9392505050565b6101cb806100916000396000f3fe608060405234801561001057600080fd5b50600436106100415760003560e01c806360598c0114610046578063a87d942c14610050578063c3da42b81461006b575b600080fd5b61004e610096565b005b6100586100f1565b6040519081526020015b60405180910390f35b60005461007e906001600160a01b031681565b6040516001600160a01b039091168152602001610062565b6000805460408051630467eef560e21b815290516001600160a01b039092169263119fbbd49260048084019382900301818387803b1580156100d757600080fd5b505af11580156100eb573d6000803e3d6000fd5b50505050565b60008060009054906101000a90046001600160a01b03166001600160a01b031663a87d942c6040518163ffffffff1660e01b815260040160206040518083038186803b15801561014057600080fd5b505afa158015610154573d6000803e3d6000fd5b505050506040513d601f19601f82011682018060405250810190610178919061017d565b905090565b60006020828403121561018e578081fd5b505191905056fea2646970667358221220220b50dea2907d54fd36279d0583e05fe9868a78b29df0e8d111fabac5ef1d9564736f6c63430008020033"

# foundation members
./build/src/defi-cli -regtest importprivkey $ownerauthpriv owner true
./build/src/defi-cli -regtest importprivkey $operatorauthpriv operator true

# push fixtures
./build/src/defi-cli -regtest importprivkey $privkey_alice
./build/src/defi-cli -regtest importprivkey $privkey_bob
./build/src/defi-cli -regtest generatetoaddress 100 $ownerauthaddr

./build/src/defi-cli -regtest utxostoaccount '{"'"$ownerauthaddr"'":"5000@DFI"}'
./build/src/defi-cli -regtest generatetoaddress 1 $ownerauthaddr

./build/src/defi-cli -regtest setgov '{"ATTRIBUTES":{"v0/params/feature/evm":"true"}}'
./build/src/defi-cli -regtest generatetoaddress 1 $ownerauthaddr

./build/src/defi-cli -regtest transferdomain 1 '{"'"$ownerauthaddr"'":["2000@DFI"]}' '{"'"$alice"'":["2000@DFI"]}'
./build/src/defi-cli -regtest generatetoaddress 1 $ownerauthaddr

curl http://localhost:19551 \
  -H 'content-type:application/json' \
  --data-binary \
  '{
    "jsonrpc":"2.0",
    "id":"fixture",
    "method":"eth_sendTransaction",
    "params":[{
      "data":"'"$contract_counter"'",
      "value":"0x00",
      "gas":"0x7a120",
      "gasPrice": "0x22ecb25c00"
    }]
  }'
./build/src/defi-cli -regtest generatetoaddress 1 $ownerauthaddr
# contract address
# 0x966aaec51a95a737d086d21f015a6991dd5559ae

curl http://localhost:19551 \
  -H 'content-type:application/json' \
  --data-binary \
  '{
    "jsonrpc":"2.0",
    "id":"fixture",
    "method":"eth_sendTransaction",
    "params":[{
      "data":"'"$contract_countercaller"'",
      "value":"0x00",
      "gas":"0x7a120",
      "gasPrice": "0x22ecb25c00"
    }]
  }'
./build/src/defi-cli -regtest generatetoaddress 1 $ownerauthaddr
# contract address
# 0x007138e9d5bdb3f0b7f3abf2d46ad4f9184ef99d
