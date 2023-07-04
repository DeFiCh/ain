#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_is_hex_string,
    assert_raises_rpc_error,
    int_to_eth_u256
)
from test_framework.evm_contract import EVMContract
from test_framework.evm_key_pair import KeyPair


class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            ['-dummypos=0', '-txnotokens=0', '-amkheight=50', '-bayfrontheight=51', '-eunosheight=80', '-fortcanningheight=82', '-fortcanninghillheight=84', '-fortcanningroadheight=86', '-fortcanningcrunchheight=88', '-fortcanningspringheight=90', '-fortcanninggreatworldheight=94', '-fortcanningepilogueheight=96', '-grandcentralheight=101', '-nextnetworkupgradeheight=105', '-subsidytest=1', '-txindex=1'],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.nodes[0].generate(200)

        self.nodes[0].utxostoaccount({self.address: "1000@DFI"})
        self.nodes[0].setgov({"ATTRIBUTES": {'v0/params/feature/evm': 'true'}})
        self.nodes[0].generate(1)

        self.key_pair = KeyPair.from_node(self.nodes[0])
        self.evmaddr = self.key_pair.address

        self.nodes[0].transferdomain([{"src": {"address":self.address, "amount":"300@DFI", "domain": 2},
                                       "dst":{"address":self.evmaddr, "amount":"300@DFI", "domain": 3}}])
        self.nodes[0].generate(1)

    def should_create_and_call_contract(self):
        from solcx import compile_standard
        import os

        contract_sol = "CounterCaller.sol"
        contract_name = "CounterCaller"
        # contract_sol = "SimpleStorage.sol"
        # contract_name = "Test"

        with open(f"{os.path.dirname(__file__)}/contracts/{contract_sol}", "r") as file:
            counter_file = file.read()

        compiled_sol = compile_standard(
        {
            "language": "Solidity",
            "sources": {contract_sol: {"content": counter_file}},
            "settings": {
                "outputSelection": {
                    "*": {
                        "*": ["abi", "metadata", "evm.bytecode", "evm.bytecode.sourceMap"]
                    }
                }
            },
        },
            solc_version="0.8.20",
        )

        data = compiled_sol["contracts"][contract_sol][contract_name]
        abi = data["abi"]
        bytecode = data["evm"]["bytecode"]["object"]

        from web3 import Web3

        w3 = Web3(Web3.HTTPProvider(self.nodes[0].get_evm_rpc()))
        chain_id = w3.eth.chain_id

        Counter = w3.eth.contract(abi=abi, bytecode=bytecode)

        nonce = w3.eth.get_transaction_count(self.evmaddr)

        # Submit the transaction that deploys the contract
        transaction = Counter.constructor().build_transaction(
            {
                # "from": self.evmaddr,
                "chainId": chain_id,
                # "gasPrice": w3.eth.gas_price,
                'gas': 500_000,
                'maxPriorityFeePerGas': 152_000_000_000,
                'maxFeePerGas': 150_000_000_000,
                "nonce": nonce,
            }
        )
        # Sign the transaction
        signed_txn = w3.eth.account.sign_transaction(transaction, self.key_pair.pkey)
        print('signed_txn: ', signed_txn.rawTransaction.hex())
        print("Deploying Contract!")
        # Send it!
        tx_hash = w3.eth.send_raw_transaction(signed_txn.rawTransaction)
        # Wait for the transaction to be mined, and get the transaction receipt
        print("Waiting for transaction to finish...")

        self.nodes[0].generate(1)

        tx_receipt = w3.eth.wait_for_transaction_receipt(tx_hash)
        print(f"Done! Contract deployed to {tx_receipt.contractAddress}")

    def create_contracts(self):
        node = self.nodes[0]

        counter_compiled = EVMContract.from_file("Counter.sol", "Counter").compile()
        counter_contract = node.evm.contract(counter_compiled)
        self.Counter = node.evm.deploy(counter_contract.constructor(), self.key_pair, counter_compiled)

        counter_caller_compiled = EVMContract.from_file("CounterCaller.sol", "CounterCaller").compile()
        contract = node.evm.contract(counter_caller_compiled)
        self.CounterCaller = node.evm.deploy(contract.constructor(self.Counter.address), self.key_pair, counter_compiled)

        # assert_equal(self.Counter.address, self.CounterCaller.address)

    def test_contract_env_global_vars(self):
        node = self.nodes[0]

        b1hash = self.Counter.functions.getBlockHash(1).call().hex()
        b1 = node.eth_getBlockByNumber(1, False)
        assert_equal(f"0x{b1hash}", b1["hash"])

        n = self.Counter.functions.getBlockNumber().call()
        bn = node.eth_blockNumber()
        assert_equal(f"0x{n}", bn)

        gas_limit = self.Counter.functions.getGasLimit().call()
        assert_equal(gas_limit, 30_000_000)

    def test_contract_state(self):
        node = self.nodes[0]

        count = self.Counter.functions.getCount().call()
        assert_equal(count, 0)

        tx = self.Counter.functions.setCount(5).build_transaction({
            'nonce': node.evm.get_transaction_count(self.evmaddr),
            'gas': 500_000,
            'maxPriorityFeePerGas': 152_000_000_000,
            'maxFeePerGas': 150_000_000_000,
        })

        signed = node.evm.sign(tx, self.key_pair.pkey)

        hash = node.evm.send_raw_tx(signed.rawTransaction)

        node.generate(1)

        node.evm.wait(hash)

        count = self.Counter.functions.getCount().call()
        assert_equal(count, 5)

        tx = self.Counter.functions.incr().build_transaction({
            'nonce': node.evm.get_transaction_count(self.evmaddr),
            'gas': 500_000,
            'maxPriorityFeePerGas': 152_000_000_000,
            'maxFeePerGas': 150_000_000_000,
        })

        signed = node.evm.sign(tx, self.key_pair.pkey)

        hash = node.evm.send_raw_tx(signed.rawTransaction)

        node.generate(1)

        node.evm.wait(hash)

        count = self.Counter.functions.getCount().call()
        assert_equal(count, 6)

    def test_calling_other_contract(self):
        node = self.nodes[0]

        count = self.CounterCaller.functions.getCount().call()
        assert_equal(count, 6)

        tx = self.CounterCaller.functions.incr().build_transaction({
            'nonce': node.evm.get_transaction_count(self.evmaddr),
            'gas': 500_000,
            'maxPriorityFeePerGas': 152_000_000_000,
            'maxFeePerGas': 150_000_000_000,
        })

        signed = node.evm.sign(tx, self.key_pair.pkey)

        hash = node.evm.send_raw_tx(signed.rawTransaction)

        node.generate(1)

        node.evm.wait(hash)

        count = self.CounterCaller.functions.getCount().call()
        assert_equal(count, 7)

    def run_test(self):
        self.setup()

        # self.should_create_and_call_contract()

        self.create_contracts()

        self.test_contract_env_global_vars()

        self.test_contract_state()

        self.test_calling_other_contract()

        # simply get contract properties
        name = self.Counter.functions.name().call()
        assert(name, "Counter")

        # owner = self.Counter.functions.owner().call()
        # print('owner: ', owner)

        # simply computing
        res = self.Counter.functions.mul(2,3).call()
        assert_equal(res, 6)

        # validation
        res = self.Counter.functions.max10(11).call()
        print('res: ', res)


if __name__ == '__main__':
    EVMTest().main()
