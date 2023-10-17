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
        self.ethPrivKey = (
            "af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23"
        )
        self.toAddress = "0x6c34cbb9219d8caa428835d2073e8ec88ba0a110"
        self.nodes[0].importprivkey(self.ethPrivKey)  # ethAddress
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
        self.nodes[0].generate(1)

        self.nodes[0] = self.nodes[0]
        self.evm_key_pair = EvmKeyPair.from_node(self.nodes[0])
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
        self.nodes[0].generate(1)
        self.start_height = self.nodes[0].getblockcount()

    def execute_transfer_tx_with_estimate_gas(self):
        self.rollback_to(self.start_height)

        nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        tx_without_gas_specified = {
            "nonce": hex(nonce),
            "from": self.ethAddress,
            "to": self.toAddress,
            "value": "0xDE0B6B3A7640000",  # 1 DFI
            "gasPrice": "0x5D21DBA00",  # 25_000_000_000
        }

        # Send tx without gas specified
        hash = self.nodes[0].eth_sendTransaction(tx_without_gas_specified)
        self.nodes[0].generate(1)

        # Verify tx is successful
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_equal(receipt["status"], "0x1")
        assert_equal(receipt["gasUsed"], "0x5208")

        # Get gas estimate
        estimate_gas = self.nodes[0].eth_estimateGas(tx_without_gas_specified)
        assert_equal(estimate_gas, "0x5208")  # 21000

        nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        tx_with_gas_specified = {
            "nonce": hex(nonce),
            "from": self.ethAddress,
            "to": self.toAddress,
            "value": "0xDE0B6B3A7640000",  # 1 DFI
            "gas": "0x5208",
            "gasPrice": "0x5D21DBA00",  # 25_000_000_000
        }

        # Send tx with exact gas specified
        hash = self.nodes[0].eth_sendTransaction(tx_with_gas_specified)
        self.nodes[0].generate(1)

        # Verify tx is successful
        receipt = self.nodes[0].eth_getTransactionReceipt(hash)
        assert_equal(receipt["status"], "0x1")
        assert_equal(receipt["gasUsed"], "0x5208")

    def execute_contract_call_tx_with_estimate_gas(self):
        self.rollback_to(self.start_height)

        # Deploy TestEstimateGas contract
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
        signed = self.nodes[0].w3.eth.account.sign_transaction(
            tx, self.evm_key_pair.privkey
        )
        hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
        self.nodes[0].generate(1)

        # Verify contract deployment is successful
        receipt = self.nodes[0].w3.eth.wait_for_transaction_receipt(hash)
        test_estimate_gas_address = receipt["contractAddress"]
        test_estimate_gas_contract = self.nodes[0].w3.eth.contract(
            address=test_estimate_gas_address, abi=abi
        )

        tx_with_excess_gas_specified = {
            "chainId": self.nodes[0].w3.eth.chain_id,
            "nonce": self.nodes[0].w3.eth.get_transaction_count(self.ethAddress),
            "from": self.ethAddress,
            "gas": "0x8952",  # 35154
        }

        # Send contract call tx with excess gas specified
        withdraw_with_exact_gas_specified_tx = (
            test_estimate_gas_contract.functions.withdraw().build_transaction(
                tx_with_excess_gas_specified
            )
        )
        signed = self.nodes[0].w3.eth.account.sign_transaction(
            withdraw_with_exact_gas_specified_tx, self.ethPrivKey
        )
        hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
        self.nodes[0].generate(1)

        # Verify tx is successful
        receipt = self.nodes[0].w3.eth.wait_for_transaction_receipt(hash)
        # assert_equal(receipt["status"], 1)
        assert_equal(receipt["gasUsed"], 32938)

        # Verify balances
        sender_balance = int(self.nodes[0].eth_getBalance(self.ethAddress)[2:], 16)
        contract_balance = int(
            self.nodes[0].eth_getBalance(self.evm_key_pair.address)[2:], 16
        )
        assert_equal(sender_balance, 99999621213000000000)
        assert_equal(contract_balance, 49997752920000000000)

        # Get gas estimate
        estimate_gas_limit = (
            test_estimate_gas_contract.functions.withdraw().estimate_gas(
                tx_with_excess_gas_specified
            )
        )
        assert_equal(estimate_gas_limit, 26238)

        # Send contract call tx with exact gas specified
        tx_with_gas_specified = {
            "chainId": self.nodes[0].w3.eth.chain_id,
            "nonce": self.nodes[0].w3.eth.get_transaction_count(self.ethAddress),
            "from": self.ethAddress,
            "gas": "0x80AA",  # 32938
        }
        withdraw_with_exact_gas_specified_tx = (
            test_estimate_gas_contract.functions.withdraw().build_transaction(
                tx_with_gas_specified
            )
        )
        signed = self.nodes[0].w3.eth.account.sign_transaction(
            withdraw_with_exact_gas_specified_tx, self.ethPrivKey
        )
        hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
        self.nodes[0].generate(1)

        # Verify tx is successful
        receipt = self.nodes[0].w3.eth.wait_for_transaction_receipt(hash)
        assert_equal(receipt["status"], 1)
        assert_equal(receipt["gasUsed"], 26238)

        # Verify balances
        sender_balance = int(self.nodes[0].eth_getBalance(self.ethAddress)[2:], 16)
        contract_balance = int(
            self.nodes[0].eth_getBalance(self.evm_key_pair.address)[2:], 16
        )
        assert_equal(sender_balance, 99999319476000000000)
        assert_equal(contract_balance, 49997752920000000000)

    def run_test(self):
        self.setup()

        self.execute_transfer_tx_with_estimate_gas()

        self.execute_contract_call_tx_with_estimate_gas()


if __name__ == "__main__":
    EVMFeeTest().main()
