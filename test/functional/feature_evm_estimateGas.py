#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""

from test_framework.test_framework import DefiTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error
from test_framework.evm_contract import EVMContract
from test_framework.evm_key_pair import EvmKeyPair

import math
from decimal import Decimal


class EVMFeeTest(DefiTestFramework):
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
        self.ethAddress = "0x9b8a4af42140d8a4c153a822f02571a1dd037e89"
        self.toAddress = "0x6c34cbb9219d8caa428835d2073e8ec88ba0a110"
        self.nodes[0].importprivkey(
            "af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23"
        )  # ethAddress
        self.nodes[0].importprivkey(
            "17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35"
        )  # toAddress

        # Generate chain
        self.nodes[0].generate(101)

        assert_raises_rpc_error(
            -32600,
            "called before Metachain height",
            self.nodes[0].evmtx,
            self.ethAddress,
            0,
            21,
            21000,
            self.toAddress,
            0.1,
        )

        # Move to fork height
        self.nodes[0].generate(4)

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

        self.node = self.nodes[0]

        self.evm_key_pair = EvmKeyPair.from_node(self.node)

        self.node.transferdomain(
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
        self.node.generate(1)

    def run_test(self):
        self.setup()

        self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.ethAddress,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                }
            ]
        )

        abi, bytecode, _ = EVMContract.from_file(
            "TestEstimateGas.sol", "TestEstimateGas"
        ).compile()
        compiled = self.nodes[0].w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": self.nodes[0].w3.eth.chain_id,
                "nonce": self.nodes[0].w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 1_000_000,
                "value": 1_000_000_000,
            }
        )

        signed = self.node.w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )

        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)

        self.node.generate(1)

        receipt = self.node.w3.eth.wait_for_transaction_receipt(hash)

        test_estimate_gas_address = receipt["contractAddress"]

        test_estimate_gas_contract = self.node.w3.eth.contract(
            address=test_estimate_gas_address, abi=abi
        )

        estimate_gas_limit = (
            test_estimate_gas_contract.functions.withdraw().estimate_gas(
                {"from": self.evm_key_pair.address}
            )
        )

        assert_equal(estimate_gas_limit, 30438)

        withdraw_with_exact_gas_specified_tx = (
            test_estimate_gas_contract.functions.withdraw().build_transaction(
                {
                    "chainId": self.node.w3.eth.chain_id,
                    "nonce": self.node.w3.eth.get_transaction_count(
                        self.evm_key_pair.address
                    ),
                    "gas": "0x76e6",  # 30438
                }
            )
        )

        signed = self.node.w3.eth.account.sign_transaction(
            withdraw_with_exact_gas_specified_tx, self.evm_key_pair.privkey
        )

        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)
        self.node.generate(1)

        receipt = self.node.w3.eth.wait_for_transaction_receipt(hash)

        assert_equal(receipt["status"], 1)
        assert_equal(receipt["gasUsed"], 30438)

        withdraw_without_exact_gas_specified_tx = (
            test_estimate_gas_contract.functions.withdraw().build_transaction(
                {
                    "chainId": self.node.w3.eth.chain_id,
                    "nonce": self.node.w3.eth.get_transaction_count(
                        self.evm_key_pair.address
                    ),
                }
            )
        )

        signed = self.node.w3.eth.account.sign_transaction(
            withdraw_without_exact_gas_specified_tx, self.evm_key_pair.privkey
        )

        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)
        self.node.generate(1)

        receipt = self.node.w3.eth.wait_for_transaction_receipt(hash)

        assert_equal(receipt["status"], 1)
        assert_equal(receipt["gasUsed"], 30438)


if __name__ == "__main__":
    EVMFeeTest().main()
