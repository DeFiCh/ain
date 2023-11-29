#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    int_to_eth_u256,
)

# pragma solidity ^0.8.2;
# contract Multiply {
#     function multiply(uint a, uint b) public pure returns (uint) {
#         return a * b;
#     }
# }
CONTRACT_BYTECODE = "0x608060405234801561001057600080fd5b5060df8061001f6000396000f3fe6080604052348015600f57600080fd5b506004361060285760003560e01c8063165c4a1614602d575b600080fd5b603c6038366004605f565b604e565b60405190815260200160405180910390f35b600060588284607f565b9392505050565b600080604083850312156070578182fd5b50508035926020909101359150565b600081600019048311821515161560a457634e487b7160e01b81526011600452602481fd5b50029056fea2646970667358221220223df7833fd08eb1cd3ce363a9c4cb4619c1068a5f5517ea8bb862ed45d994f764736f6c63430008020033"


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
            ],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.ethAddress = "0x9b8a4af42140d8a4c153a822f02571a1dd037e89"
        self.toAddress = "0x6c34cbb9219d8caa428835d2073e8ec88ba0a110"
        self.nodes[0].importprivkey(
            "af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23"
        )  # ethAddress
        self.nodes[0].importprivkey(
            "17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35"
        )  # toAddress

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
                        "address": self.ethAddress,
                        "amount": "50@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)

        balance = self.nodes[0].eth_getBalance(self.ethAddress, "latest")
        assert_equal(balance, int_to_eth_u256(50))

    def create_block(self, count, priority_fees):
        node = self.nodes[0]
        nonce = 0
        for x in range(count):
            for y in priority_fees:
                tx = {
                    "from": self.ethAddress,
                    "value": "0x0",
                    "data": CONTRACT_BYTECODE,
                    "gas": "0x18e70",  # 102_000
                    "maxPriorityFeePerGas": hex(y),
                    "maxFeePerGas": "0x22ecb25c00",  # 150_000_000_000
                    "type": "0x2",
                    "nonce": hex(nonce),
                }
                nonce += 1
                node.eth_sendTransaction(tx)
            node.generate(1)

    def test_fee_history(self):
        node = self.nodes[0]

        self.create_block(2, [1, 2, 3, 4, 5, 6, 7, 8, 9, 10])

        count = 2
        reward_percentiles = [20, 30, 50, 70, 85, 100]
        history = node.eth_feeHistory(hex(count), "latest", reward_percentiles)

        current = node.eth_blockNumber()
        assert_equal(history["oldestBlock"], hex(int(current, 16) - count + 1))
        assert_equal(len(history["baseFeePerGas"]), count + 1)
        for x in history["reward"]:
            assert_equal(len(x), len(reward_percentiles))
            assert_equal(x, ["0x2", "0x3", "0x5", "0x7", "0x9", "0xa"])

    def run_test(self):
        self.setup()

        self.test_fee_history()


if __name__ == "__main__":
    EVMTest().main()
