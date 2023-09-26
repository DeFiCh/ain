#!/usr/bin/env python3
# Copyright (c) 2014-2019 The Bitcoin Core developers
# Copyright (c) DeFi Blockchain Developers
# Distributed under the MIT software license, see the accompanying
# file LICENSE or http://www.opensource.org/licenses/mit-license.php.
"""Test EVM behaviour"""


from test_framework.evm_key_pair import EvmKeyPair
from test_framework.evm_contract import EVMContract
from test_framework.test_framework import DefiTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    get_solc_artifact_path,
)


class EIP1559Fees(DefiTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [
            [
                "-txordering=2",
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
                "-nextnetworkupgradeheight=105",
                "-subsidytest=1",
                "-txindex=1",
            ]
        ]

    def setup(self):
        self.address = self.nodes[0].get_genesis_keys().ownerAuthAddress
        self.node = self.nodes[0]
        self.node.generate(105)
        self.evm_key_pair = EvmKeyPair.from_node(self.node)

        # Activate EVM
        self.node.setgov(
            {
                "ATTRIBUTES": {
                    "v0/params/feature/evm": "true",
                    "v0/params/feature/transferdomain": "true",
                }
            }
        )
        self.node.utxostoaccount({self.address: "10@DFI"})
        self.node.generate(2)

        self.node.transferdomain(
            [
                {
                    "src": {"address": self.address, "amount": "10@DFI", "domain": 2},
                    "dst": {
                        "address": self.evm_key_pair.address,
                        "amount": "10@DFI",
                        "domain": 3,
                    },
                }
            ]
        )
        self.node.generate(1)

        self.contract = EVMContract.from_file("Events.sol", "TestEvents")
        abi, bytecode, _ = self.contract.compile()
        compiled = self.node.w3.eth.contract(abi=abi, bytecode=bytecode)

        tx = compiled.constructor().build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(self.evm_key_pair.address),
                "maxFeePerGas": 10_000_000_000,
                "maxPriorityFeePerGas": 500_000,
                "gas": 1_000_000,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)

        self.node.generate(1)

        receipt = self.node.w3.eth.wait_for_transaction_receipt(hash)
        self.contract_address = receipt["contractAddress"]
        self.contract = self.node.w3.eth.contract(
            address=receipt["contractAddress"], abi=abi
        )

    def check_tx_fee(self):
        before_balance = self.node.w3.eth.get_balance(self.evm_key_pair.address)
        # send TX
        tx = self.contract.functions.store(10).build_transaction(
            {
                "chainId": self.node.w3.eth.chain_id,
                "nonce": self.node.w3.eth.get_transaction_count(
                    self.evm_key_pair.address
                ),
                "gasPrice": 10_000_000_000,
            }
        )
        signed = self.node.w3.eth.account.sign_transaction(tx, self.evm_key_pair.privkey)
        hash = self.node.w3.eth.send_raw_transaction(signed.rawTransaction)
        self.node.generate(1)
        receipt = self.node.w3.eth.wait_for_transaction_receipt(hash)

        after_balance = self.node.w3.eth.get_balance(self.evm_key_pair.address)
        assert_equal(before_balance - after_balance, receipt["gasUsed"] * receipt["effectiveGasPrice"])

    def run_test(self):
        self.setup()

        self.check_tx_fee()


if __name__ == "__main__":
    EIP1559Fees().main()
