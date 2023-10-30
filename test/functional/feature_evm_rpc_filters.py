#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.evm_contract import EVMContract
from test_framework.evm_key_pair import EvmKeyPair
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    int_to_eth_u256,
)


class EVMTest(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-dummypos=0",
                "-txnotokens=0",
                "-amkheight=50",
                "-bayfrontheight=51",
                "-dakotaheight=51",
                "-eunosheight=80",
                "-fortcanningheight=82",
                "-fortcanninghillheight=84",
                "-fortcanningroadheight=86",
                "-fortcanningcrunchheight=88",
                "-fortcanningspringheight=90",
                "-fortcanninggreatworldheight=94",
                "-fortcanningepilogueheight=96",
                "-grandcentralheight=101",
                "-metachainheight=105",
                "-subsidytest=1",
                "-txindex=1",
            ],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.evm_key_pair = EvmKeyPair.from_node(self.nodes[0])

        # Generate chain
        self.nodes[0].generate(105)

        self.nodes[0].getbalance()
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                    "v0/params/feature/transferdomain": "true",
                    "v0/transferdomain/dvm-evm/enabled": "true",
                    "v0/transferdomain/dvm-evm/src-formats": ["p2pkh", "bech32"],
                    "v0/transferdomain/dvm-evm/dest-formats": ["erc55"],
                    "v0/transferdomain/evm-dvm/src-formats": ["erc55"],
                    "v0/transferdomain/evm-dvm/auth-formats": ["bech32-erc55"],
                    "v0/transferdomain/evm-dvm/dest-formats": ["p2pkh", "bech32"],
                }
            }
        )
        self.nodes[0].generate(2)

        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "50@DFI", "domain": 2},
                    "dst": {
                        "address": self.evm_key_pair.address,
                        "amount": "50@DFI",
                        "domain": 3,
                    },
                }
            ]
        )
        self.nodes[0].generate(1)

        balance = self.nodes[0].eth_getBalance(self.evm_key_pair.address, "latest")
        assert_equal(balance, int_to_eth_u256(50))

    def create_contract(self):
        node = self.nodes[0]

        self.contract = EVMContract.from_file("Events.sol", "TestEvents")
        abi, bytecode, _ = self.contract.compile()
        compiled = node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 500_000,
                "gas": 1_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)

        node.generate(1)

        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        self.contract_address = receipt["contractAddress"]
        self.contract = node.w3.eth.contract(
            address=receipt["contractAddress"], abi=abi
        )

    def create_block(self, count):
        node = self.nodes[0]
        for x in range(count):
            tx = self.contract.functions.store(10).build_transaction(
                {
                    "chainId": node.w3.eth.chain_id,
                    "nonce": node.w3.eth.get_transaction_count(
                        self.evm_key_pair.address
                    ),
                    "gasPrice": 10_000_000_000,
                }
            )
            signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
            hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)
            node.generate(1)
            receipt = node.w3.eth.wait_for_transaction_receipt(hash)
            assert_equal(len(receipt["logs"]), 1)

    def test_get_logs(self):
        node = self.nodes[0]

        logs = node.eth_getLogs(
            {
                "fromBlock": "earliest",
                "toBlock": "latest",
            }
        )
        assert_equal(len(logs), 5)

    def test_get_filter_logs(self):
        node = self.nodes[0]

        id = node.eth_newFilter(
            {
                "fromBlock": "earliest",
                "toBlock": "latest",
                "address": self.contract_address,
            }
        )

        logs = node.eth_getFilterLogs(id)
        assert_equal(len(logs), 3)

    def test_get_filter_changes(self):
        node = self.nodes[0]

        id = node.eth_newFilter(
            {
                "fromBlock": "earliest",
                "toBlock": "latest",
                "address": self.contract_address,
            }
        )
        logs = node.eth_getFilterChanges(id)
        assert_equal(len(logs), 3)

        logs = node.eth_getFilterChanges(id)
        assert_equal(len(logs), 0)

    def test_new_filter(self):
        node = self.nodes[0]

        id1 = node.eth_newFilter(
            {
                "fromBlock": "0x0",
                "toBlock": "0x1",
            }
        )
        id2 = node.eth_newFilter(
            {
                "fromBlock": "0x1",
                "toBlock": "0x2",
            }
        )
        assert_equal(hex(int(id1, 16) + 1), id2)

    def fail_new_filter_to_greater_than_from(self):
        assert_raises_rpc_error(
            -32001,
            "Custom error: fromBlock is greater than toBlock",
            self.nodes[0].eth_newFilter,
            {"fromBlock": "0x1", "toBlock": "0x0"},
        )

    def fail_new_filter_unavailable_block(self):
        assert_raises_rpc_error(
            -32001,
            "Custom error: block not found",
            self.nodes[0].eth_newFilter,
            {"fromBlock": "0x1", "toBlock": "0x999999999"},
        )

    def run_test(self):
        self.setup()

        self.create_contract()

        self.create_block(3)

        self.test_get_logs()

        self.test_get_filter_logs()

        self.test_get_filter_changes()

        self.test_new_filter()

        self.fail_new_filter_to_greater_than_from()

        self.fail_new_filter_unavailable_block()

        self.fail_new_filter_unavailable_block()


if __name__ == "__main__":
    EVMTest().main()
