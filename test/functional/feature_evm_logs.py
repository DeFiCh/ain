#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM contract"""

from test_framework.util import (
    assert_equal,
    int_to_eth_u256,
)
from test_framework.test_framework import DefiTestFramework
from test_framework.evm_contract import EVMContract
from test_framework.evm_key_pair import EvmKeyPair
from web3._utils.events import get_event_data


class EVMTestLogs(DefiTestFramework):
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

    def should_create_contract(self):
        node = self.nodes[0]
        self.evm_key_pair = EvmKeyPair.from_node(node)

        node.transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "50@DFI", "domain": 2},
                    "dst": {
                        "address": self.evm_key_pair.address,
                        "amount": "50@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        node.generate(1)

        self.contract = EVMContract.from_file("Events.sol", "TestEvents")
        abi, bytecode, _ = self.contract.compile()
        compiled = node.w3.eth.contract(abi=abi, bytecode=bytecode)
        self.abi = abi
        self.event_abi = compiled._find_matching_event_abi("NumberStored")

        tx = compiled.constructor().build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 1_500_000_000,
                "gas": 1_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)

        node.generate(1)

        receipt = node.w3.eth.wait_for_transaction_receipt(hash)
        self.contract = node.w3.eth.contract(
            address=receipt["contractAddress"], abi=abi
        )

    def should_contract_store_and_emit_logs(self):
        # store
        node = self.nodes[0]
        tx = self.contract.functions.store(10).build_transaction(
            {
                "chainId": node.w3.eth.chain_id,
                "from": self.evm_key_pair.address,
                "nonce": node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "gasPrice": 10_000_000_000,
            }
        )
        signed = node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = node.w3.eth.send_raw_transaction(signed.rawTransaction)

        node.generate(1)

        node.w3.eth.wait_for_transaction_receipt(hash)

        block = self.nodes[0].eth_getBlockByNumber("latest")
        receipt = self.nodes[0].eth_getTransactionReceipt(block["transactions"][0])
        assert_equal(len(receipt["logs"]), 1)

        logs = self.nodes[0].eth_getLogs({"fromBlock": "latest"})
        assert_equal(logs[0]["blockHash"], block["hash"])
        assert_equal(logs[0]["blockNumber"], block["number"])
        assert_equal(logs[0]["logIndex"], "0x0")
        assert_equal(logs[0]["removed"], False)
        assert_equal(logs[0]["transactionHash"], block["transactions"][0])
        assert_equal(logs[0]["transactionIndex"], "0x0")
        assert_equal(logs[0]["data"], "0x")
        assert_equal(len(logs[0]["topics"]), 3)

        logs = node.w3.eth.get_logs({"fromBlock": "latest"})
        event_data = get_event_data(node.w3.codec, self.event_abi, logs[0])
        assert_equal(event_data["event"], "NumberStored")

    def transferdomain_receipt_gas_used(self):
        dvmHash = self.nodes[0].transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "50@DFI", "domain": 2},
                    "dst": {
                        "address": self.evm_key_pair.address,
                        "amount": "50@DFI",
                        "domain": 3,
                    },
                    "singlekeycheck": False,
                }
            ]
        )
        self.nodes[0].generate(1)
        evmHash = self.nodes[0].vmmap(dvmHash, 5)["output"]
        receipt = self.nodes[0].eth_getTransactionReceipt(evmHash)
        assert_equal(receipt["gasUsed"], int_to_eth_u256(0))

    def run_test(self):
        self.setup()

        self.should_create_contract()

        self.should_contract_store_and_emit_logs()

        self.transferdomain_receipt_gas_used()


if __name__ == "__main__":
    EVMTestLogs().main()
