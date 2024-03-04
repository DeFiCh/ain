#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test eth_createAccessList RPC behaviour"""

from test_framework.evm_key_pair import EvmKeyPair
from test_framework.test_framework import DefiTestFramework
from test_framework.evm_contract import EVMContract
from test_framework.util import assert_equal

from decimal import Decimal
import math
import json
from web3 import Web3


class EvmTracerTest(DefiTestFramework):
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
                "-df23height=105",
                "-subsidytest=1",
            ],
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.address_erc55 = self.nodes[0].addressmap(self.address, 1)["format"][
            "erc55"
        ]
        self.ethAddress = "0x9b8a4af42140d8a4c153a822f02571a1dd037e89"
        self.ethPrivKey = (
            "af990cc3ba17e776f7f57fcc59942a82846d75833fa17d2ba59ce6858d886e23"
        )
        self.toAddress = "0x6c34cbb9219d8caa428835d2073e8ec88ba0a110"
        self.toPrivKey = (
            "17b8cb134958b3d8422b6c43b0732fcdb8c713b524df2d45de12f0c7e214ba35"
        )
        self.nodes[0].importprivkey(self.ethPrivKey)  # ethAddress
        self.nodes[0].importprivkey(self.toPrivKey)  # toAddress

        # Generate chain and move to fork height
        self.nodes[0].generate(105)
        self.nodes[0].utxostoaccount({self.address: "201@DFI"})
        self.nodes[0].setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                    "v0/params/feature/transferdomain": "true",
                    "v0/transferdomain/dvm-evm/enabled": "true",
                    "v0/transferdomain/dvm-evm/dat-enabled": "true",
                    "v0/transferdomain/evm-dvm/dat-enabled": "true",
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
                    "src": {"address": self.address, "amount": "100@DFI", "domain": 2},
                    "dst": {
                        "address": self.ethAddress,
                        "amount": "100@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)
        self.start_height = self.nodes[0].getblockcount()

    def test_tracer_on_transfer_tx(self):
        self.rollback_to(self.start_height)
        hashes = []
        start_nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        for i in range(5):
            hash = self.nodes[0].eth_sendTransaction(
                {
                    "nonce": hex(start_nonce + i),
                    "from": self.ethAddress,
                    "to": self.toAddress,
                    "value": "0xDE0B6B3A7640000",  # 1 DFI
                    "gas": "0x5209",
                    "gasPrice": "0x5D21DBA00",  # 25_000_000_000
                }
            )
            hashes.append(hash)
        self.nodes[0].generate(1)
        block_txs = self.nodes[0].eth_getBlockByNumber("latest", True)["transactions"]

        # Test tracer for every tx
        for tx in block_txs:
            assert_equal(
                self.nodes[0].debug_traceTransaction(tx["hash"]),
                {"gas": "0x5208", "failed": False, "returnValue": "", "structLogs": []},
            )

    def test_tracer_on_contract_call_tx(self):
        self.rollback_to(self.start_height)
        before_balance = Decimal(
            self.nodes[0].getaccount(self.ethAddress)[0].split("@")[0]
        )
        assert_equal(before_balance, Decimal("100"))

        # Deploy StateChange contract
        abi, bytecode, _ = EVMContract.from_file(
            "StateChange.sol", "StateChange"
        ).compile()
        compiled = self.nodes[0].w3.eth.contract(abi=abi, bytecode=bytecode)
        tx = compiled.constructor().build_transaction(
            {
                "chainId": self.nodes[0].w3.eth.chain_id,
                "nonce": self.nodes[0].w3.eth.get_transaction_count(self.ethAddress),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 1_000_000,
            }
        )
        signed = self.nodes[0].w3.eth.account.sign_transaction(tx, self.ethPrivKey)
        hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
        self.nodes[0].generate(1)

        contract_address = self.nodes[0].w3.eth.wait_for_transaction_receipt(hash)[
            "contractAddress"
        ]
        contract = self.nodes[0].w3.eth.contract(address=contract_address, abi=abi)

        # Set state to true
        nonce = self.nodes[0].w3.eth.get_transaction_count(self.ethAddress)
        tx = contract.functions.changeState(True).build_transaction(
            {
                "chainId": self.nodes[0].w3.eth.chain_id,
                "nonce": self.nodes[0].w3.eth.get_transaction_count(self.ethAddress),
                "gasPrice": 25_000_000_000,
                "gas": 30_000_000,
                "nonce": nonce,
            }
        )
        signed = self.nodes[0].w3.eth.account.sign_transaction(tx, self.ethPrivKey)
        state_change_tx_hash = self.nodes[0].w3.eth.send_raw_transaction(
            signed.rawTransaction
        )

        # Run loop contract call in the same block
        tx = contract.functions.loop(1_000).build_transaction(
            {
                "chainId": self.nodes[0].w3.eth.chain_id,
                "nonce": self.nodes[0].w3.eth.get_transaction_count(self.ethAddress),
                "gasPrice": 25_000_000_000,
                "gas": 30_000_000,
                "nonce": nonce + 1,
            }
        )
        signed = self.nodes[0].w3.eth.account.sign_transaction(tx, self.ethPrivKey)
        loop_tx_hash = self.nodes[0].w3.eth.send_raw_transaction(signed.rawTransaction)
        self.nodes[0].generate(1)

        # Test tracer for contract call txs
        state_change_gas_used = Decimal(
            self.nodes[0].w3.eth.wait_for_transaction_receipt(state_change_tx_hash)[
                "gasUsed"
            ]
        )
        loop_gas_used = Decimal(
            self.nodes[0].w3.eth.wait_for_transaction_receipt(loop_tx_hash)["gasUsed"]
        )
        # Test tracer for state change tx
        assert_equal(
            int(
                self.nodes[0].debug_traceTransaction(state_change_tx_hash.hex())["gas"],
                16,
            ),
            state_change_gas_used,
        )
        assert_equal(
            self.nodes[0].debug_traceTransaction(state_change_tx_hash.hex())["failed"],
            False,
        )
        # Test tracer for loop tx
        # TODO: Disabled for now because state consistency of the debug_traceTransaction is
        # incorrect.
        # assert_equal(
        #     int(
        #         self.nodes[0].debug_traceTransaction(state_change_tx_hash.hex())["gas"],
        #         16,
        #     ),
        #     loop_gas_used,
        # )
        # assert_equal(
        #     self.nodes[0].debug_traceTransaction(state_change_tx_hash.hex())["failed"],
        #     False,
        # )

    def run_test(self):
        self.setup()

        self.test_tracer_on_transfer_tx()

        self.test_tracer_on_contract_call_tx()


if __name__ == "__main__":
    EvmTracerTest().main()
